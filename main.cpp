#include <fcntl.h>
#include <pty.h>
#include <unistd.h>
#include <utmp.h>

#include <vterm.h>

#include <QtCore>

#include "capture.h"


class Process : public QProcess {
public:
    void setup(int col, int row) {
        struct winsize s;
        s.ws_col = col;
        s.ws_row = row;
        s.ws_xpixel = 0;
        s.ws_ypixel = 0;
        if (openpty(&masterFd, &slaveFd, nullptr, nullptr, &s) == -1) {
            perror("openpty");
            exit(1);
        }
        fcntl(masterFd, F_SETFD, FD_CLOEXEC);
        fcntl(slaveFd, F_SETFD, FD_CLOEXEC);
    }

    void setupChildProcess() override {
        fcntl(slaveFd, F_SETFD, 0); // remove FD_CLOEXEC
        login_tty(slaveFd);
    }

    int masterFd;
    int slaveFd;
};


class Main {
public:
    ~Main() {
        vterm_free(vterm);
        vterm = nullptr;
    }

public:
    void setup(const QStringList &args) {
        vterm = vterm_new(24, 80);
        vterm_set_utf8(vterm, 1);
        VTermScreen *vts = vterm_obtain_screen(vterm);
        vterm_screen_enable_altscreen(vts, 1);

        screenCallbacks.damage = nullptr;
        screenCallbacks.moverect = nullptr;
        screenCallbacks.movecursor = nullptr;
        screenCallbacks.settermprop = [](VTermProp prop, VTermValue *val, void *user) {
            static_cast<Main*>(user)->settermprop(prop, val);
            return 1;
        };
        screenCallbacks.bell = nullptr;
        screenCallbacks.resize = nullptr;
        screenCallbacks.sb_pushline = [](int, const VTermScreenCell*, void*) { return 0; }; // this does not seem to be nullable
        screenCallbacks.sb_popline = nullptr;


        vterm_screen_set_callbacks(vts, &screenCallbacks, this);

        unhandled.text = nullptr;
        unhandled.control = [] (unsigned char control, void *user) {
            (void)control;(void)user;
            return 1;
        };
        unhandled.escape = nullptr;
        unhandled.csi = [] (const char *leader, const long args[], int argcount, const char *intermed, char command, void *user) {
            (void)leader;(void)args;(void)argcount;(void)intermed;(void)command;(void)user;
            return 1;
        };
        unhandled.osc = [] (const char *command, size_t cmdlen, void *user) {
            (void)command;(void)cmdlen;(void)user;
            return 1;
        };
        unhandled.dcs = [] (const char *command, size_t cmdlen, void *user) {
            (void)command;(void)cmdlen;(void)user;
            return 1;
        };
        unhandled.resize = nullptr;

        vterm_screen_set_unrecognised_fallbacks(vts, &unhandled, this);

        vterm_screen_reset(vts, 1);

        process.setup(80, 24);

        vterm_output_set_callback(vterm, [] (const char *s, size_t len, void *user) {
            Process &process = *static_cast<Process*>(user);
            write(process.masterFd, s, len);
        }, &process);

        QSocketNotifier *masterRead = new QSocketNotifier(process.masterFd, QSocketNotifier::Read);
        QObject::connect(masterRead, &QSocketNotifier::activated, [this] {
            char buff[1000];
            ssize_t num = ::read(process.masterFd, buff, 1000);
            vterm_input_write(vterm, buff, num);
        });

        QSocketNotifier *ttyRead = new QSocketNotifier(controlFd, QSocketNotifier::Read);
        QObject::connect(ttyRead, &QSocketNotifier::activated, [this] {
            char buff[1000];
            ssize_t num = ::read(0, buff, 1000);
            controlData(buff, num);
        });

        QObject::connect(&process, qOverload<int>(&QProcess::finished), [this] {
            const char msg[] = "*exited";
            write(controlFd, msg, sizeof(msg));
        });

        process.setProgram(args[0]);
        process.setArguments(args.mid(1));
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.start();
    }

private:
    void sendToInterior(char* data, ssize_t len) {
        ::write(process.masterFd, data, static_cast<size_t>(len));
    }

    void captureAll() {
        // missing from public api: pending wrap state.
        QString data = "{\n";
        VTermState *state = vterm_obtain_state(vterm);
        VTermPos pos;
        vterm_state_get_cursorpos(state, &pos);
        data += QStringLiteral("  \"cursor_column\": %0,\n  \"cursor_row\": %1,\n")
                .arg(QString::number(pos.col), QString::number(pos.row));

        QString sgrData;

        VTermValue vtval;

        vterm_state_get_penattr(state, VTERM_ATTR_BOLD, &vtval);
        if (vtval.boolean) sgrData += QStringLiteral("\n    \"bold\": true,");

        vterm_state_get_penattr(state, VTERM_ATTR_UNDERLINE, &vtval);
        if (vtval.number == VTERM_UNDERLINE_SINGLE) sgrData += QStringLiteral("\n    \"underline\": true,");
        if (vtval.number == VTERM_UNDERLINE_DOUBLE) sgrData += QStringLiteral("\n    \"double_underline\": true,");
        if (vtval.number == VTERM_UNDERLINE_CURLY) sgrData += QStringLiteral("\n    \"curly_underline\": true,");

        vterm_state_get_penattr(state, VTERM_ATTR_ITALIC, &vtval);
        if (vtval.boolean) sgrData += QStringLiteral("\n    \"italic\": true,");

        vterm_state_get_penattr(state, VTERM_ATTR_BLINK, &vtval);
        if (vtval.boolean) sgrData += QStringLiteral("\n    \"blink\": true,");

        vterm_state_get_penattr(state, VTERM_ATTR_REVERSE, &vtval);
        if (vtval.boolean) sgrData += QStringLiteral("\n    \"inverse\": true,");

        vterm_state_get_penattr(state, VTERM_ATTR_STRIKE, &vtval);
        if (vtval.boolean) sgrData += QStringLiteral("\n    \"strike\": true,");

        QString colorAsString;
        vterm_state_get_penattr(state, VTERM_ATTR_FOREGROUND, &vtval);
        colorAsString = formatColor(vtval.color);
        if (colorAsString.size()) {
            sgrData += QStringLiteral("\n    \"fg\": \"%0\",").arg(colorAsString);
        }

        vterm_state_get_penattr(state, VTERM_ATTR_BACKGROUND, &vtval);
        colorAsString = formatColor(vtval.color);
        if (colorAsString.size()) {
            sgrData += QStringLiteral("\n    \"bg\": \"%0\",").arg(colorAsString);
        }
        if (sgrData.size()) {
            data += QStringLiteral("  \"current_sgr_attr\": {");
            sgrData.chop(1);
            data += sgrData;
            data += QStringLiteral("  },\n");
        }

        if (!cursorVisible) {
            data += QStringLiteral("  \"cursor_visible\": false,\n");
        }

        if (cursorBlink) {
            data += QStringLiteral("  \"cursor_blink\": true,\n");
        } else {
            data += QStringLiteral("  \"cursor_blink\": false,\n");
        }

        if (cursorShape == VTERM_PROP_CURSORSHAPE_BLOCK) data += QStringLiteral("  \"cursor_shape\": \"block\",\n");
        if (cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) data += QStringLiteral("  \"cursor_shape\": \"underline\",\n");
        if (cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) data += QStringLiteral("  \"cursor_shape\": \"bar\",\n");

        if (mouseMode == VTERM_PROP_MOUSE_CLICK) data += QStringLiteral("  \"mouse_mode\": \"clicks\",\n");
        if (mouseMode == VTERM_PROP_MOUSE_DRAG) data += QStringLiteral("  \"mouse_mode\": \"drag\",\n");
        if (mouseMode == VTERM_PROP_MOUSE_MOVE) data += QStringLiteral("  \"mouse_mode\": \"movement\",\n");

        if (altScreen) data += QStringLiteral("  \"alternate_screen\": true,\n");
        if (inverse) data += QStringLiteral("  \"inverse_screen\": true,\n");

        if (titleSet) data += QStringLiteral("  \"title\": \"%0\",\n").arg(formatString(title));
        if (iconTitleSet) data += QStringLiteral("  \"icon_title\": \"%0\",\n").arg(formatString(iconTitle));

        data += captureAsJsonWithoutOuterbraces(vterm, inverse) + "}";
        QByteArray msg = data.toUtf8();
        msg.append('\0');
        write(controlFd, msg.data(), msg.size());
    }

    void controlData(char* data, ssize_t len) {
        pendingControlData.append(data, static_cast<int>(len));
        while (true) {
            int end = pendingControlData.indexOf('\0');
            if (end != -1) {
                QByteArray cmd = pendingControlData.left(end);
                pendingControlData.remove(0, end + 1);

                //qDebug() << "cmd" << cmd;
                if (cmd == "capture:img") {
                    QByteArray msg = captureAsJson(vterm, inverse).toUtf8();
                    msg.append('\0');
                    write(controlFd, msg.data(), msg.size());
                } else if (cmd == "capture:all") {
                    captureAll();
                } else if (cmd.startsWith("send-to-interior:")) {
                    QByteArray hex = cmd.mid(17);
                    QByteArray output = QByteArray::fromHex(hex);
                    write(process.masterFd, output.data(), output.size());
                } else if (cmd == "reset") {
                    VTermScreen *vts = vterm_obtain_screen(vterm);
                    vterm_screen_reset(vts, 1);
                    write(controlFd, "", 1);
                } else if (cmd == "quit") {
                    process.terminate();
                    process.waitForFinished();
                    write(controlFd, "", 1);
                    QCoreApplication::instance()->quit();
                }
            } else {
                break;
            }
        }
    }

    void settermprop(VTermProp prop, VTermValue *val) {
        if (prop == VTERM_PROP_CURSORVISIBLE) {
            cursorVisible = val->boolean;
        } else if (prop == VTERM_PROP_CURSORBLINK) {
            cursorBlink = val->boolean;
        } else if (prop == VTERM_PROP_ALTSCREEN) {
            altScreen = val->boolean;
        } else if (prop == VTERM_PROP_TITLE) {
            titleSet = true;
            title = QString::fromUtf8(val->string);
        } else if (prop == VTERM_PROP_ICONNAME) {
            iconTitleSet = true;
            iconTitle = QString::fromUtf8(val->string);
        } else if (prop == VTERM_PROP_REVERSE) {
            inverse = val->boolean;
        } else if (prop == VTERM_PROP_CURSORSHAPE) {
            cursorShape = val->number;
        } else if (prop == VTERM_PROP_MOUSE) {
            mouseMode = val->number;
        }
    }

private:
    VTerm *vterm = nullptr;
    Process process;
    int controlFd = 0; // defaults to stdin
    QByteArray pendingControlData;
    VTermScreenCallbacks screenCallbacks;
    VTermParserCallbacks unhandled;

    // buffered screen state
    bool cursorVisible = true;
    bool cursorBlink = 1;
    bool altScreen = false;
    bool inverse = false;
    bool titleSet = false;
    QString title;
    bool iconTitleSet = false;
    QString iconTitle;
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int mouseMode = VTERM_PROP_MOUSE_NONE;
};


int main(int argc, char** argv) {

    QCoreApplication app(argc, argv);

    if (getenv("DRIVER_WAIT")) {
        sleep(10);
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
    parser.addPositionalArgument("command", "Command to execute and connect to the terminal");
    parser.addOption({ "control-via-fd0", "control communication is via fd 0."});
    parser.process(app);
    const QStringList args = parser.positionalArguments();

    if (!parser.isSet("control-via-fd0")) {
        parser.showHelp();
    }

    Main m;
    m.setup(args);

    app.exec();

    return 0;
}

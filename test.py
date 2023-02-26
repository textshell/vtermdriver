#! /usr/bin/python3

import binascii
import json
import selectors
import socket
import subprocess
import sys

import unittest

driver = None


def run_in_terminal(args, *, cmds=[]):
    outer, inner = socket.socketpair()
    sel = selectors.DefaultSelector()
    sel.register(outer, selectors.EVENT_READ, 1)
    proc = subprocess.Popen([driver, '--control-via-fd0', *args], stdin=inner)
    inner.close()
    if cmds:
        for cmd in cmds:
            outer.send(cmd + b'\0')
    outer.setblocking(False)
    exit = False
    response_data = bytearray()
    result = None
    notifications = []
    try:
        while not exit:
            events = sel.select()
            for key, mask in events:
                if key.data == 1:
                    data = outer.recv(1000)
                    if not data:
                        exit = True
                        break
                    response_data += data
                    idx = response_data.find(0)
                    while idx != -1:
                        message = response_data[0:idx]
                        del response_data[0:idx+1]
                        idx = response_data.find(0)
                        if message and message[0] == ord('{'):
                            result = json.loads(message.decode())
                            outer.send(b'quit\0')
                        elif message.startswith(b'*exited:'):
                            outer.send(b'capture:all\0')
                        elif message.startswith(b'*'):
                            notifications.append(message.decode())
    finally:
        outer.close()
        proc.kill()
        proc.wait(0.5)

    cellMap = {}

    for cell in result['cells']:
        cellMap[cell['x'], cell['y']] = cell

    return cellMap, result, notifications


foreground_test_cases = [
    ('tput setaf 0', 'black'),
    ('tput setaf 1', 'red'),
    ('tput setaf 2', 'green'),
    ('tput setaf 3', 'yellow'),
    ('tput setaf 4', 'blue'),
    ('tput setaf 5', 'magenta'),
    ('tput setaf 6', 'cyan'),
    ('tput setaf 7', 'white'),
    ('printf "\033[90m"', 'bright black'),
    ('printf "\033[91m"', 'bright red'),
    ('printf "\033[92m"', 'bright green'),
    ('printf "\033[93m"', 'bright yellow'),
    ('printf "\033[94m"', 'bright blue'),
    ('printf "\033[95m"', 'bright magenta'),
    ('printf "\033[96m"', 'bright cyan'),
    ('printf "\033[97m"', 'bright white'),
    # vterm does not track named colors and low indexed colors separately
    ('printf "\033[38:5:1m"', 'red'),
    ('printf "\033[38:5:50m"', '50'),
    ('printf "\033[38:2:0:0:0m"', '#000000'),
    ('printf "\033[38:2:23:12:255m"', '#170cff'),
    ('printf "\033[38:2:255:255:255m"', '#ffffff'),
]

background_test_cases = [
    ('tput setab 0', 'black'),
    ('tput setab 1', 'red'),
    ('tput setab 2', 'green'),
    ('tput setab 3', 'yellow'),
    ('tput setab 4', 'blue'),
    ('tput setab 5', 'magenta'),
    ('tput setab 6', 'cyan'),
    ('tput setab 7', 'white'),
    ('printf "\033[100m"', 'bright black'),
    ('printf "\033[101m"', 'bright red'),
    ('printf "\033[102m"', 'bright green'),
    ('printf "\033[103m"', 'bright yellow'),
    ('printf "\033[104m"', 'bright blue'),
    ('printf "\033[105m"', 'bright magenta'),
    ('printf "\033[106m"', 'bright cyan'),
    ('printf "\033[107m"', 'bright white'),
    # vterm does not track named colors and low indexed colors separately
    ('printf "\033[48:5:1m"', 'red'),
    ('printf "\033[48:5:50m"', '50'),
    ('printf "\033[48:2:0:0:0m"', '#000000'),
    ('printf "\033[48:2:23:12:255m"', '#170cff'),
    ('printf "\033[48:2:255:255:255m"', '#ffffff'),
]


class Tests(unittest.TestCase):
    def cmpCell(self, cellMap, x, y, *, ch=' ', width=1, fg=None, bg=None,
                bold=False, italic=False, blink=False, inverse=False, strike=False, underline=False,
                double_underline=False, curly_underline=False):
        cell = cellMap[x, y]
        self.assertEqual(cell['t'], ch)
        self.assertFalse('cleared' in cell)
        if width != 1:
            self.assertTrue('width' in cell)
            self.assertEqual(cell['width'], width)
        else:
            self.assertFalse('width' in cell)

        if fg is not None:
            self.assertTrue('fg' in cell)
            self.assertEqual(cell['fg'], fg)
        else:
            self.assertFalse('fg' in cell)

        if bg is not None:
            self.assertTrue('bg' in cell)
            self.assertEqual(cell['bg'], bg)
        else:
            self.assertFalse('bg' in cell)

        if bold:
            self.assertTrue('bold' in cell)
            self.assertEqual(cell['bold'], True)
        else:
            self.assertFalse('bold' in cell)

        if italic:
            self.assertTrue('italic' in cell)
            self.assertEqual(cell['italic'], True)
        else:
            self.assertFalse('italic' in cell)

        if blink:
            self.assertTrue('blink' in cell)
            self.assertEqual(cell['blink'], True)
        else:
            self.assertFalse('blink' in cell)

        if inverse:
            self.assertTrue('inverse' in cell)
            self.assertEqual(cell['inverse'], True)
        else:
            self.assertFalse('inverse' in cell)

        if strike:
            self.assertTrue('strike' in cell)
            self.assertEqual(cell['strike'], True)
        else:
            self.assertFalse('strike' in cell)

        if underline:
            self.assertTrue('underline' in cell)
            self.assertEqual(cell['underline'], True)
        else:
            self.assertFalse('underline' in cell)

        if double_underline:
            self.assertTrue('double_underline' in cell)
            self.assertEqual(cell['double_underline'], True)
        else:
            self.assertFalse('double_underline' in cell)

        if curly_underline:
            self.assertTrue('curly_underline' in cell)
            self.assertEqual(cell['curly_underline'], True)
        else:
            self.assertFalse('curly_underline' in cell)

    def cmpSGR(self, data, *, fg=None, bg=None,
               bold=False, italic=False, blink=False, inverse=False, strike=False, underline=False,
               double_underline=False, curly_underline=False):

        if 'current_sgr_attr' not in data:
            sgr = {}
        else:
            sgr = data['current_sgr_attr']
            self.assertTrue(len(sgr) > 0)

        if fg is not None:
            self.assertTrue('fg' in sgr)
            self.assertEqual(sgr['fg'], fg)
        else:
            self.assertFalse('fg' in sgr)

        if bg is not None:
            self.assertTrue('bg' in sgr)
            self.assertEqual(sgr['bg'], bg)
        else:
            self.assertFalse('bg' in sgr)

        if bold:
            self.assertTrue('bold' in sgr)
            self.assertEqual(sgr['bold'], True)
        else:
            self.assertFalse('bold' in sgr)

        if italic:
            self.assertTrue('italic' in sgr)
            self.assertEqual(sgr['italic'], True)
        else:
            self.assertFalse('italic' in sgr)

        if blink:
            self.assertTrue('blink' in sgr)
            self.assertEqual(sgr['blink'], True)
        else:
            self.assertFalse('blink' in sgr)

        if inverse:
            self.assertTrue('inverse' in sgr)
            self.assertEqual(sgr['inverse'], True)
        else:
            self.assertFalse('inverse' in sgr)

        if strike:
            self.assertTrue('strike' in sgr)
            self.assertEqual(sgr['strike'], True)
        else:
            self.assertFalse('strike' in sgr)

        if underline:
            self.assertTrue('underline' in sgr)
            self.assertEqual(sgr['underline'], True)
        else:
            self.assertFalse('underline' in sgr)

        if double_underline:
            self.assertTrue('double_underline' in sgr)
            self.assertEqual(sgr['double_underline'], True)
        else:
            self.assertFalse('double_underline' in sgr)

        if curly_underline:
            self.assertTrue('curly_underline' in sgr)
            self.assertEqual(sgr['curly_underline'], True)
        else:
            self.assertFalse('curly_underline' in sgr)

    def test_cleared(self):
        cellMap, _, _ = run_in_terminal(['echo', ''])
        cell = cellMap[0, 0]
        self.assertTrue('cleared' in cell)
        self.assertEqual(cell['cleared'], True)

    def test_text_test(self):
        cellMap, _, _ = run_in_terminal(['echo', 'aあ'])
        self.cmpCell(cellMap, 0, 0, ch='a')
        self.cmpCell(cellMap, 1, 0, ch='あ', width=2)

    def test_bold_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput bold; echo -n bold'])
        self.cmpCell(cellMap, 0, 0, ch='b', bold=True)
        self.cmpCell(cellMap, 1, 0, ch='o', bold=True)
        self.cmpCell(cellMap, 2, 0, ch='l', bold=True)
        self.cmpCell(cellMap, 3, 0, ch='d', bold=True)

    def test_blink_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput blink; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', blink=True)

    def test_italic_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput sitm; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', italic=True)

    def test_inverse_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput rev; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', inverse=True)

    def test_strike_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'printf "\033[9m"; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', strike=True)

    def test_underline_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput smul; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', underline=True)

    def test_double_underline_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'printf "\033[21m"; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', double_underline=True)

    def test_curly_underline_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'printf "\033[4:3m"; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', curly_underline=True)

    def test_fg_test(self):
        for cmd, expected in foreground_test_cases:
            with self.subTest(color=expected):
                cellMap, _, _ = run_in_terminal(['bash', '-c', cmd + '; echo -n X'])
                self.cmpCell(cellMap, 0, 0, ch='X', fg=expected)

    def test_bg_test(self):
        for cmd, expected in background_test_cases:
            with self.subTest(color=expected):
                cellMap, _, _ = run_in_terminal(['bash', '-c', cmd + '; echo -n X'])
                self.cmpCell(cellMap, 0, 0, ch='X', bg=expected)

    def test_fgbg_test(self):
        cellMap, _, _ = run_in_terminal(['bash', '-c', 'tput setaf 1; tput setab 2; echo -n X'])
        self.cmpCell(cellMap, 0, 0, ch='X', fg='red', bg='green')

    def test_bold_attr(self):
        _, res, _ = run_in_terminal(['tput', 'bold'])
        self.cmpSGR(res, bold=True)

    def test_blink_attr(self):
        _, res, _ = run_in_terminal(['tput', 'blink'])
        self.cmpSGR(res, blink=True)

    def test_italic_attr(self):
        _, res, _ = run_in_terminal(['tput', 'sitm'])
        self.cmpSGR(res, italic=True)

    def test_inverse_attr(self):
        _, res, _ = run_in_terminal(['tput', 'rev'])
        self.cmpSGR(res, inverse=True)

    def test_strike_attr(self):
        _, res, _ = run_in_terminal(['printf', '\033[9m'])
        self.cmpSGR(res, strike=True)

    def test_underline_attr(self):
        _, res, _ = run_in_terminal(['tput', 'smul'])
        self.cmpSGR(res, underline=True)

    def test_double_underline_attr(self):
        _, res, _ = run_in_terminal(['printf', '\033[21m'])
        self.cmpSGR(res, double_underline=True)

    def test_curly_underline_attr(self):
        _, res, _ = run_in_terminal(['printf', '\033[4:3m'])
        self.cmpSGR(res, curly_underline=True)

    def test_fg_attr(self):
        for cmd, expected in foreground_test_cases:
            with self.subTest(color=expected):
                _, res, _ = run_in_terminal(['bash', '-c', cmd])
                self.cmpSGR(res, fg=expected)

    def test_bg_attr(self):
        for cmd, expected in background_test_cases:
            with self.subTest(color=expected):
                _, res, _ = run_in_terminal(['bash', '-c', cmd])
                self.cmpSGR(res, bg=expected)

    def test_fgbg_attr(self):
        _, res, _ = run_in_terminal(['bash', '-c', 'tput setaf 1; tput setab 2'])
        self.cmpSGR(res, fg='red', bg='green')

    def test_softwarp(self):
        _, res, _ = run_in_terminal(['bash', '-c', 'echo ' + 'x' * 120])
        self.assertEqual(list(res['lines'].keys()), ['0'])
        self.assertTrue('soft_wrapped' in res['lines']['0'])
        self.assertTrue(res['lines']['0']['soft_wrapped'])

    def test_cursor_hide(self):
        _, res, _ = run_in_terminal(['tput', 'civis'])
        self.assertEqual(res['cursor_visible'], False)

    def test_cursor_blink(self):
        _, res, _ = run_in_terminal(['printf', '\033[1 q'])
        self.assertEqual(res['cursor_blink'], True)

    def test_cursor_blink_off(self):
        _, res, _ = run_in_terminal(['printf', '\033[2 q'])
        self.assertEqual(res['cursor_blink'], False)

    def test_cursor_shape_block(self):
        _, res, _ = run_in_terminal(['printf', '\033[2 q'])
        self.assertEqual(res['cursor_shape'], 'block')

    def test_cursor_shape_bar(self):
        _, res, _ = run_in_terminal(['printf', '\033[5 q'])
        self.assertEqual(res['cursor_shape'], 'bar')

    def test_cursor_shape_underline(self):
        _, res, _ = run_in_terminal(['printf', '\033[3 q'])
        self.assertEqual(res['cursor_shape'], 'underline')

    def test_inverse_screen(self):
        _, res, _ = run_in_terminal(['printf', '\033[?5h'])
        self.assertEqual(res['inverse_screen'], True)

    def test_inverse_screen_inv(self):
        cellMap, res, _ = run_in_terminal(['bash', '-c', 'printf "\033[?5h"; tput rev; echo -n X'])
        self.assertEqual(res['inverse_screen'], True)
        self.cmpCell(cellMap, 0, 0, ch='X', inverse=True)

    def test_cursor_position(self):
        _, res, _ = run_in_terminal(['tput', 'cup', '5', '12'])
        self.assertEqual(res['cursor_column'], 12)
        self.assertEqual(res['cursor_row'], 5)

    def test_alt_screen(self):
        _, res, _ = run_in_terminal(['printf', '\033[?1049h'])
        self.assertEqual(res['alternate_screen'], True)

    def test_title(self):
        _, res, _ = run_in_terminal(['printf', '\033]1;asdzt\033\\'])
        self.assertEqual(res['icon_title'], 'asdzt')

    def test_icon(self):
        _, res, _ = run_in_terminal(['printf', '\033]2;asdyt\033\\'])
        self.assertEqual(res['title'], 'asdyt')

    def test_mouse_mode_clicks(self):
        _, res, _ = run_in_terminal(['printf', '\033[?1000h'])
        self.assertEqual(res['mouse_mode'], 'clicks')

    def test_mouse_mode_drag(self):
        _, res, _ = run_in_terminal(['printf', '\033[?1002h'])
        self.assertEqual(res['mouse_mode'], 'drag')

    def test_mouse_mode_movement(self):
        _, res, _ = run_in_terminal(['printf', '\033[?1003h'])
        self.assertEqual(res['mouse_mode'], 'movement')

    def test_metadata(self):
        _, res, _ = run_in_terminal(['printf', ''])
        self.assertEqual(res['width'], 80)
        self.assertEqual(res['height'], 24)
        self.assertEqual(res['version'], 0)

    def test_send(self):
        cellMap, _, _ = run_in_terminal(['cat'], cmds=[(b'send-to-interior:' + binascii.b2a_hex('aあ\r\n\x04'.encode()))])
        self.cmpCell(cellMap, 0, 0, ch='a')
        self.cmpCell(cellMap, 1, 0, ch='あ', width=2)

    def test_bell(self):
        _, res, notifications = run_in_terminal(['printf', '\a'])
        self.assertEqual(notifications, ['*bell'])


def main():
    global driver
    argv = sys.argv
    for i in range(1, len(argv)):
        if argv[i].startswith('--driver='):
            driver = argv[i][9:]
            del argv[i]
            break
        elif argv[i] == '--driver':
            driver == argv[i + 1]
            del argv[i + 1]
            del argv[i]
            break

    if not driver:
        print("A --driver=path argument is required")
        sys.exit(1)

    unittest.main(argv=argv)


if __name__ == '__main__':
    main()

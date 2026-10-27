import base64
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('configure',Path(__file__).resolve().parents[1]/'tools/configure.py')
cfg=importlib.util.module_from_spec(spec);spec.loader.exec_module(cfg)

class Port:
    def __init__(self,identity=True):self.identity=identity;self.sent=[];self.response=bytearray()
    def reset_input_buffer(self):self.response.clear()
    def flush(self):pass
    def write(self,data):
        self.sent.append(data)
        self.response=bytearray((b'\r\nSDWAN ESP32-S3 USB modem emulator' if self.identity and data==b'ATI\r' else b'\r\nOther device' if data==b'ATI\r' else b'')+b'\r\nOK\r\n')
    def read(self,n):
        if not self.response:return b''
        result=bytes(self.response[:n]);del self.response[:n];return result

class ConfigureTests(unittest.TestCase):
    def test_open_does_not_reset_ch343(self):
        class Device:
            def open(self):
                self.opened=True
                assert self.dtr is False and self.rts is False and self.port=='COM5'
        device=Device();serial=Mock();serial.Serial.return_value=device
        with patch.object(cfg.os,'name','nt'):
            self.assertIs(cfg.open_port(serial,'COM5'),device)
        serial.Serial.assert_called_once_with(port=None,baudrate=115200,timeout=.2,write_timeout=2)
        self.assertTrue(device.opened)
    def test_open_failure_closes_handle(self):
        device=Mock();device.open.side_effect=OSError('busy');serial=Mock();serial.Serial.return_value=device
        with self.assertRaises(OSError):cfg.open_port(serial,'COM5')
        device.close.assert_called_once()
    def test_posix_port_is_exclusive(self):
        serial=Mock()
        with patch.object(cfg.os,'name','posix'):cfg.open_port(serial,'/dev/ttyUSB0')
        self.assertTrue(serial.Serial.call_args.kwargs['exclusive'])
    def test_encoded_ssid_and_special_key(self):
        ssid='A, "B"';commands=cfg.profile_commands(ssid,'test-"only-key')
        token=commands[0].split('wifi.')[1].split('"')[0]
        self.assertEqual(base64.b32decode(token+'='*((-len(token))%8)),ssid.encode())
        self.assertIn('test-""only-key',commands[1])
    def test_utf8_ssid(self):
        self.assertTrue(cfg.profile_commands('Тест WiFi','test-only-key')[0].startswith('AT+CGDCONT='))
        with self.assertRaises(ValueError):cfg.profile_commands('Я'*17,'test-only-key')
    def test_bounds(self):
        for ssid in ['', 's'*33, 'x\nAT&W', 'x\0']:
            with self.subTest(ssid=ssid),self.assertRaises(ValueError):cfg.profile_commands(ssid,'test-only-key')
        for key in ['short','x'*65,'ключ-пароль','x\n'*8,'g'*64]:
            with self.subTest(key=key),self.assertRaises(ValueError):cfg.profile_commands('AP',key)
        self.assertEqual(len(cfg.profile_commands('s'*32,'a'*64)),3)
    def test_no_secrets_before_identity(self):
        port=Port(False)
        with self.assertRaisesRegex(RuntimeError,'identity mismatch'):cfg.configure(port,'AP','test-only-key')
        self.assertEqual(port.sent,[b'ATE0\r',b'ATI\r'])
    def test_persistence_explicit_only(self):
        port=Port();cfg.configure(port,'AP','test-only-key');self.assertNotIn(b'AT&W\r',port.sent)
        port=Port();cfg.configure(port,'AP','test-only-key',save=True);self.assertEqual(port.sent[-1],b'AT&W\r')
    def test_open_network(self):
        self.assertIn('AT+CGAUTH=1,0,"",""',cfg.profile_commands('Open','',open_network=True))
        with self.assertRaises(ValueError):cfg.profile_commands('Open','unexpected',open_network=True)
    def test_error_response_does_not_expose_credentials(self):
        class ErrorPort(Port):
            def write(self,data):self.response=bytearray(b'\r\n+CME ERROR: 30 test-only-key\r\n')
        with self.assertRaises(RuntimeError) as error:cfg.exchange(ErrorPort(),'AT')
        self.assertNotIn('test-only-key',str(error.exception))
        self.assertIn('withheld',str(error.exception))
    def test_configuration_failure_identifies_stage_without_secret(self):
        class ErrorPort(Port):
            def write(self,data):
                if data.startswith(b'AT+CGAUTH='):self.response=bytearray(b'\r\nERROR\r\n')
                else:super().write(data)
        with self.assertRaisesRegex(RuntimeError,'Stage authentication') as error:
            cfg.configure(ErrorPort(),'AP','test-only-key')
        self.assertNotIn('test-only-key',str(error.exception))
    def test_boot_panic_is_reported_without_log_or_credentials(self):
        class BootPort(Port):
            def write(self,data):self.response=bytearray(b'\r\nassert failed: private-test-key')
        with self.assertRaisesRegex(RuntimeError,'restarted or panicked') as error:
            cfg.exchange(BootPort(),'AT')
        self.assertNotIn('private-test-key',str(error.exception))
    def test_timeout_is_bounded(self):
        class EmptyPort(Port):
            def read(self,n):return b''
        with patch.object(cfg.time,'monotonic',side_effect=[0,0,100]):
            with self.assertRaisesRegex(RuntimeError,'timeout'):cfg.exchange(EmptyPort(),'AT',activation=True)
    def test_oversize_response_is_bounded(self):
        class LongPort(Port):
            def write(self,data):self.response=bytearray(b'x'*2050)
        with self.assertRaisesRegex(RuntimeError,'oversized'):cfg.exchange(LongPort(),'AT')

if __name__=='__main__':unittest.main()

"""Independent libmbim decoder, used on synthetic host test fixtures only."""
import ctypes
import ctypes.util
import os
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parent
def verify(folder):
    path=os.environ.get('MODEM_LIBMBIM_PATH') or ctypes.util.find_library('mbim-glib')
    if not path:raise RuntimeError('Install libmbim-glib4 or set MODEM_LIBMBIM_PATH for independent fixture decoding')
    lib=ctypes.CDLL(str(path));glib=ctypes.CDLL('libglib-2.0.so.0')
    lib.mbim_message_new.argtypes=[ctypes.c_void_p,ctypes.c_uint32];lib.mbim_message_new.restype=ctypes.c_void_p
    lib.mbim_message_validate.argtypes=[ctypes.c_void_p,ctypes.c_void_p];lib.mbim_message_validate.restype=ctypes.c_int
    lib.mbim_message_get_printable.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int];lib.mbim_message_get_printable.restype=ctypes.c_void_p
    lib.mbim_message_unref.argtypes=[ctypes.c_void_p];glib.g_free.argtypes=[ctypes.c_void_p]
    results={}
    for f in sorted(Path(folder).glob('*.bin')):
        b=f.read_bytes();buf=ctypes.create_string_buffer(b);msg=lib.mbim_message_new(buf,len(b));assert msg
        text_ptr=None
        try:
            assert lib.mbim_message_validate(msg,None),f.name+': invalid MBIM header'
            text_ptr=lib.mbim_message_get_printable(msg,b'',False);text=ctypes.string_at(text_ptr).decode()
            is_error=struct.unpack_from('<I',b)[0]==0x80000003 and struct.unpack_from('<I',b,40)[0]!=0
            assert 'n/a:' not in text and (is_error or 'Unsupported message' not in text),f.name+': field decoder failed: '+text
            (Path(folder)/(f.stem+'.txt')).write_text(text)
            results[f.name]={'header_and_fields_decoded':True,'bytes':len(b)}
        finally:
            if text_ptr:glib.g_free(text_ptr)
            lib.mbim_message_unref(msg)
    assert len(results)>=20, 'Fixtures absent/incomplete'
    return results

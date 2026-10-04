from pathlib import Path
import ctypes, os
os.environ['SDL_VIDEODRIVER']='dummy'
os.environ['SDL_AUDIODRIVER']='dummy'
lib=ctypes.CDLL(str(Path(__file__).resolve().parents[3] / "build/lib/libRowlEngineCore.so"))
lib.RowlEngine_Create.restype=ctypes.c_void_p
lib.RowlEngine_Init.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_int]
lib.RowlEngine_ExecuteScript.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
h=lib.RowlEngine_Create()
print('handle-created', bool(h), flush=True)
print('init',lib.RowlEngine_Init(h,320,180,0),flush=True)
print('execute-enter',flush=True)
print('execute-return',lib.RowlEngine_ExecuteScript(h,b'while true do pcall(function() while true do end end) end'),flush=True)

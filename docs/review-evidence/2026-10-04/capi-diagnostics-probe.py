from pathlib import Path
import ctypes, os
os.environ['SDL_VIDEODRIVER']='dummy';os.environ['SDL_AUDIODRIVER']='dummy'
l=ctypes.CDLL(str(Path(__file__).resolve().parents[3] / "build/lib/libRowlEngineCore.so"))
l.RowlEngine_Create.restype=ctypes.c_void_p
h=l.RowlEngine_Create()
for name,args in [('RowlEngine_Init',[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_int]),('RowlEngine_SetFadeCurveChecked',[ctypes.c_void_p,ctypes.c_int]),('RowlEngine_GetLastResultCode',[ctypes.c_void_p]),('RowlEngine_EvaluateCondition',[ctypes.c_void_p,ctypes.c_char_p]),('RowlEngine_ExecuteScript',[ctypes.c_void_p,ctypes.c_char_p]),('RowlEngine_Destroy',[ctypes.c_void_p])]:getattr(l,name).argtypes=args
l.RowlEngine_Init(h,320,180,0)
print('AUDIT badFade=',l.RowlEngine_SetFadeCurveChecked(h,9),'last=',l.RowlEngine_GetLastResultCode(h),flush=True)
print('AUDIT goodFade=',l.RowlEngine_SetFadeCurveChecked(h,1),'last=',l.RowlEngine_GetLastResultCode(h),flush=True)
l.RowlEngine_ExecuteScript(h,b'x = 10')
print('AUDIT conditionMutate=',l.RowlEngine_EvaluateCondition(h,b'(function() x = 20; math.abs = nil; return true end)()'),flush=True)
print('AUDIT existingGlobalMutated=',l.RowlEngine_EvaluateCondition(h,b'x == 20'),'stdlibMutated=',l.RowlEngine_EvaluateCondition(h,b'math.abs == nil'),flush=True)
l.RowlEngine_Destroy(h)

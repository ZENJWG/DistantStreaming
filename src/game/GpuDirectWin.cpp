// Distant Streaming GPU-direct mode: do not intercept or proxy D3D9 resources.
// SanFrameOptimizer may query this ABI to determine whether a texture/buffer
// is an Distant Streaming facade. The answer is always false in direct mode.
extern "C" __declspec(dllexport)
int __cdecl ASC_IsNativeResource(const void* /*resource*/) noexcept {
    return 0;
}

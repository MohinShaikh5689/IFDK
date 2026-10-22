Public headers of FireBurn/FSR-Vulkan at c64f093 (MIT, `LICENSE.txt`), the commit gow3_android uses.
The iOS build has no FSR 3/4: `../ffx_stub.c` implements the calls gow3gpu makes and fails them, so
the temporal upscaler takes its existing fallback (TAA or native) exactly as on a GPU without support.

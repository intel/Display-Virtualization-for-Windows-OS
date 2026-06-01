#pragma once

#if !DBG

#undef INFO
#undef DBGPRINT
#undef FuncTrace
#undef TRACING

#define INFO(...) ((void)0)
#define DBGPRINT(...) ((void)0)
#define FuncTrace(...) ((void)0)
#define TRACING() ((void)0)

#endif

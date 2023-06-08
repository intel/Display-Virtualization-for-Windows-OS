#pragma once

#define WPP_CONTROL_GUIDS WPP_DEFINE_CONTROL_GUID(DVEnablerGuid, (5E6BE9AC, 16AC, 40C9, BBC1, A7D39E3F463F), \
									 WPP_DEFINE_BIT(verbose) \
									 WPP_DEFINE_BIT(information) \
									 WPP_DEFINE_BIT(error) \
									 WPP_DEFINE_BIT(ftrace) \
									 WPP_DEFINE_BIT(warning))

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                             \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                            \
    (WPP_LEVEL_ENABLED(flag) &&                                        \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags)                              \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags)                            \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)


class tracer {
private:
	char* m_func_name;
public:
	tracer(const char* func_name);
	~tracer();
};
#define TRACING() tracer trace(__FUNCTION__)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC ERR{LEVEL=TRACE_LEVEL_ERROR,FLAGS=error}(MSG,...);
// FUNC WARN{LEVEL=TRACE_LEVEL_WARNING,FLAGS=warning}(MSG,...);
// FUNC WARNING{LEVEL=TRACE_LEVEL_WARNING,FLAGS=warning}(MSG,...);
// FUNC INFO{LEVEL=TRACE_LEVEL_INFORMATION,FLAGS=information}(MSG,...);
// FUNC DBGPRINT{LEVEL=TRACE_LEVEL_INFORMATION,FLAGS=verbose}(MSG,...);
// FUNC FuncTrace{LEVEL=TRACE_LEVEL_INFORMATION,FLAGS=ftrace}(MSG, ...);
// USEPREFIX(ERR, "%!STDPREFIX! [%!FUNC!:%!LINE!] [ERR] \t");
// USEPREFIX(WARN, "%!STDPREFIX! [%!FUNC!:%!LINE!] [WARN] \t");
// USEPREFIX(WARNING, "%!STDPREFIX! [%!FUNC!:%!LINE!] [WARN] \t");
// USEPREFIX(DBGPRINT, "%!STDPREFIX! [%!FUNC!:%!LINE!] [DBG] \t");
// USEPREFIX(INFO, "%!STDPREFIX! [%!FUNC!:%!LINE!] [INFO] \t");
// end_wpp


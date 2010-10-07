# pipeline-gcc-warning.m4 serial 1
dnl PIPELINE_GCC_WARNING(WARNING)
dnl Add -WWARNING to CFLAGS if it is supported by the compiler.
AC_DEFUN([PIPELINE_GCC_WARNING],
[pipeline_saved_CFLAGS="$CFLAGS"
 CFLAGS="$CFLAGS -W$1"
 AC_CACHE_CHECK([that GCC supports -W$1],
   [AS_TR_SH([pipeline_cv_gcc_warning_$1])],
   [AS_TR_SH([pipeline_cv_gcc_warning_$1])=no
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
                      [AS_TR_SH([pipeline_cv_gcc_warning_$1])=yes])])
 if test "$AS_TR_SH([pipeline_cv_gcc_warning_$1])" = no; then
   CFLAGS="$pipeline_saved_CFLAGS"
 fi]) # PIPELINE_GCC_WARNING

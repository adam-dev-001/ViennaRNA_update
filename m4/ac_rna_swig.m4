AC_DEFUN([RNA_ENABLE_SWIG_INTERFACES],[

  AC_ARG_VAR([SWIG], [Path to the 'swig' program to generate scripting language interfaces])

  AX_REQUIRE_DEFINED([AX_PKG_SWIG])

  RNA_ADD_PACKAGE([swig],
                  [SWIG scripting language interfaces],
                  [yes],[],[],
                  [${srcdir}/interfaces/Makefile.am])

  AS_IF([test "x$with_swig" != "xno"],[
    wants_swig="yes"
    AX_PKG_SWIG(4.0.2, [has_swig="yes"], [has_swig="no"])
  ],[
    wants_swig="no"
  ])

  AM_CONDITIONAL(HAS_SWIG, test "x$has_swig" != "xno")
  RNA_ENABLE_SWIG_PERL
  RNA_ENABLE_SWIG_PYTHON2
  RNA_ENABLE_SWIG_PYTHON

])


AC_DEFUN([RNA_ENABLE_SWIG_PERL],[

  RNA_ADD_PACKAGE([perl],
                  [Perl interface],
                  [yes],[],[],
                  [${srcdir}/interfaces/Perl/Makefile.am])


  ## check for perl requirements
  AS_IF([test "x$with_perl" != "xno"],[
    AS_IF([test "x$wants_swig" = "xno"],[
      with_perl="no"
    ], [
      ## if swig is not available, check whether we already have swig generated sources
      if test "x$has_swig" != "xyes"
      then
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Perl/RNA_wrap.cpp],[],[
          with_perl="no"
        ])
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Perl/RNA.pm],[],[
          with_perl="no"
        ])
      fi
    ])
  ])

  RNA_PACKAGE_IF_ENABLED([perl],[
    AX_PERL_EXT
    if test "x$PERL" = "x"; then
      AC_MSG_ERROR([Perl is required to build.])
      [enable_perl_status="Perl is required to build."]
    fi
  ])

  # prepare all files for perl interface
  RNA_PACKAGE_IF_ENABLED([perl],[
    # Compose the correct installation path for perl modules
    #
    # here we actually have to account for INSTALLDIRS env variable, which can be
    #
    # site    = where the local systems administrator installs packages to
    # vendor  = where system packages are installed to, or
    # core    = where perl core packages are installed
    #
    # The default selection is 'site', but upon packaging for a specific distribution
    # we might want the user to set this to 'vendor'
    #
    AS_IF([ test "x$INSTALLDIRS" == "xvendor" ],[
      PERL_ARCH_RELATIVE_INSTALL_DIR=`echo ${PERL_EXT_VENDORARCH} | sed "s,${PERL_EXT_VENDORPREFIX},,"`
      PERL_LIB_RELATIVE_INSTALL_DIR=`echo ${PERL_EXT_VENDORLIB} | sed "s,${PERL_EXT_VENDORPREFIX},,"`
      ],[
      PERL_ARCH_RELATIVE_INSTALL_DIR=`echo ${PERL_EXT_SITEARCH} | sed "s,${PERL_EXT_SITEPREFIX},,"`
      PERL_LIB_RELATIVE_INSTALL_DIR=`echo ${PERL_EXT_SITELIB} | sed "s,${PERL_EXT_SITEPREFIX},,"`
    ])
    AC_SUBST(PERL_ARCH_RELATIVE_INSTALL_DIR)
    AC_SUBST(PERL_LIB_RELATIVE_INSTALL_DIR)

    AC_DEFINE([WITH_PERL_INTERFACE], [1], [Create the perl interface to RNAlib])
    AC_SUBST([PERL_INTERFACE], [Perl])
    AC_CONFIG_FILES([interfaces/Perl/Makefile interfaces/Perl/version.i])
  ])

])

AC_DEFUN([RNA_ENABLE_SWIG_PYTHON],[

  RNA_ADD_PACKAGE([python],
                  [Python 3.x interface],
                  [yes],[],[],
                  [${srcdir}/interfaces/Python/Makefile.am])


  ## check for python requirements
  AS_IF([test "x$with_python" != "xno"],[
    AS_IF([test "x$wants_swig" = "xno"],[
      with_python="no"
    ],[
      ## if swig is not available, check whether we already have swig generated sources
      if test "x$has_swig" != "xyes"
      then
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Python/RNA_wrap.cpp],[],[
          with_python="no"
        ])
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Python/RNA.py],[],[
          with_python="no"
        ])
      fi
    ])
  ])

  AS_IF([test "x$with_python" != "xno"],[

    ## check for python3 config
    AX_PYTHON3_DEVEL

    if test "x$python3_enabled_but_failed" != "x"
    then
      with_python="no"
    else
      AC_DEFINE([WITH_PYTHON_INTERFACE], [1], [Create the Python 3.x interface to RNAlib])
      AC_SUBST([PYTHON_INTERFACE], [Python])
    fi

    AC_CONFIG_FILES([interfaces/Python/Makefile interfaces/Python/version.i])
    AC_CONFIG_FILES([interfaces/Python/RNA/__init__.py])
    AC_CONFIG_FILES([setup.py setup.cfg pyproject.toml])
  ])
])

AC_DEFUN([RNA_ENABLE_SWIG_PYTHON2],[

  AX_REQUIRE_DEFINED([AX_PYTHON2_DEVEL])

  RNA_ADD_PACKAGE([python2],
                  [Python2 interface],
                  [no],[],[],
                  [${srcdir}/interfaces/Python2/Makefile.am])


  ## check for python2 requirements
  AS_IF([test "x$with_python2" != "xno"],[
    AS_IF([test "x$wants_swig" = "xno"],[
      with_python2="no"
    ],[
      ## if swig is not available, check whether we already have swig generated sources
      if test "x$has_swig" != "xyes"
      then
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Python2/RNA_wrap.cpp],[],[
          with_python2="no"
        ])
        AC_RNA_TEST_FILE([${srcdir}/interfaces/Python2/RNA.py],[],[
          with_python2="no"
        ])
      fi
    ])
  ])

  AS_IF([test "x$with_python2" != "xno"],[

    ## check for python2 config
    AX_PYTHON2_DEVEL

    if test "x$python2_enabled_but_failed" != "x"
    then
      with_python2="no"
    else
      AC_SUBST(PYTHON2DIR,$python2dir)
      AC_SUBST(PKGPYTHON2DIR,$pkgpython2dir)
      AC_SUBST(PYEXEC2DIR,$py2execdir)
      AC_SUBST(PKGPYEXEC2DIR,$pkgpy2execdir)

      AC_DEFINE([WITH_PYTHON2_INTERFACE], [1], [Create the Python 2.x interface to RNAlib])
      AC_SUBST([PYTHON2_INTERFACE], [Python2])
      AC_CONFIG_FILES([interfaces/Python2/Makefile interfaces/Python2/version.i])
    fi
  ])
])

MOSTLYCLEANFILES =
include $(top_srcdir)/gnuscripts/lalapps.am
include $(top_srcdir)/gnuscripts/lalsuite_help2man.am

AM_CPPFLAGS += $(PAGER_CPPFLAGS)

if CFITSIO

bin_PROGRAMS = \
	lalapps_fits_array_copy \
	lalapps_fits_array_imarith \
	lalapps_fits_array_list \
	lalapps_fits_array_stat \
	lalapps_fits_copy \
	lalapps_fits_header_delval \
	lalapps_fits_header_getval \
	lalapps_fits_header_list \
	lalapps_fits_header_setval \
	lalapps_fits_overview \
	lalapps_fits_table_calc \
	lalapps_fits_table_list \
	lalapps_fits_table_merge \
	lalapps_fits_table_select \
        $(END_OF_LIST)

lalapps_fits_array_copy_SOURCES = fits_array_copy.c
lalapps_fits_array_imarith_SOURCES = fits_array_imarith.c
lalapps_fits_array_list_SOURCES = fits_array_list.c
lalapps_fits_array_stat_SOURCES = fits_array_stat.c
lalapps_fits_copy_SOURCES = fits_copy.c
lalapps_fits_header_delval_SOURCES = fits_header_delval.c
lalapps_fits_header_getval_SOURCES = fits_header_getval.c
lalapps_fits_header_list_SOURCES = fits_header_list.c
lalapps_fits_header_setval_SOURCES = fits_header_setval.c
lalapps_fits_overview_SOURCES = fits_overview.c
lalapps_fits_table_calc_SOURCES = fits_table_calc.c
lalapps_fits_table_list_SOURCES = fits_table_list.c
lalapps_fits_table_merge_SOURCES = fits_table_merge.c
lalapps_fits_table_select_SOURCES = fits_table_select.c

endif

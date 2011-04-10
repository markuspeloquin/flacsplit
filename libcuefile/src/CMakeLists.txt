find_program(LEX_EXE flex)
if(LEX_EXE STREQUAL "LEX_EXE-NOTFOUND")
	message(FATAL_ERROR "flex not found")
endif(LEX_EXE STREQUAL "LEX_EXE-NOTFOUND")

find_program(YACC_EXE byacc)
if(YACC_EXE STREQUAL "YACC_EXE-NOTFOUND")
	message(FATAL_ERROR "byacc not found")
endif(YACC_EXE STREQUAL "YACC_EXE-NOTFOUND")

# reuseable cmake macro for yacc
MACRO(YACC_FILE _filename _prefix)
	GET_FILENAME_COMPONENT(_basename ${_filename} NAME_WE)
	ADD_CUSTOM_COMMAND(
	    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_basename}.c
		${CMAKE_CURRENT_BINARY_DIR}/${_basename}.h
	    COMMAND ${YACC_EXE} -d -p ${_prefix} -b ${_basename}
		${CMAKE_CURRENT_SOURCE_DIR}/${_filename}
	    COMMAND mv ${_basename}.tab.c ${_basename}.c
	    COMMAND mv ${_basename}.tab.h ${_basename}.h
	    DEPENDS ${_filename}
	)
ENDMACRO(YACC_FILE)

# reuseable cmake macro for lex
MACRO(LEX_FILE _filename _prefix)
	GET_FILENAME_COMPONENT(_basename ${_filename} NAME_WE)
	ADD_CUSTOM_COMMAND(
	    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_basename}.c
		${CMAKE_CURRENT_BINARY_DIR}/${_basename}.h
	    COMMAND ${LEX_EXE} -P ${_prefix}
		-o ${_basename}.c
		--header-file=${_basename}.h
		${CMAKE_CURRENT_SOURCE_DIR}/${_filename}
	    DEPENDS ${_filename}
	)
ENDMACRO(LEX_FILE)

add_definitions(-DYY_NEVER_INTERACTIVE)
include_directories(${libcuefile_SOURCE_DIR}/include)

YACC_FILE(cue_parse.y cue_yy)
LEX_FILE(cue_scan.l cue_yy)

add_library(cuefile-static STATIC cd cdtext cue_parse cue_print cue_scan cuefile time toc toc_parse toc_print toc_scan)
set_target_properties(cuefile-static PROPERTIES OUTPUT_NAME cuefile CLEAN_DIRECT_OUTPUT 1)

add_library(cuefile-shared SHARED cd cdtext cue_parse cue_print cue_scan cuefile time toc toc_parse toc_print toc_scan)
set_target_properties(cuefile-shared PROPERTIES OUTPUT_NAME cuefile CLEAN_DIRECT_OUTPUT 1 VERSION 0.0.0 SOVERSION 0)

install(TARGETS cuefile-static cuefile-shared LIBRARY DESTINATION "lib${LIB_SUFFIX}" ARCHIVE DESTINATION "lib${LIB_SUFFIX}")

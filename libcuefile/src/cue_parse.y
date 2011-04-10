%{
/*
 * cue_parse.y -- parser for cue files
 *
 * Copyright (C) 2004, 2005, 2006 Svend Sorensen
 * For license terms, see the file COPYING in this distribution.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cuetools/cd.h>
#include "time.h"

#define YYDEBUG 1

extern int yylex();
void yyerror (char *s);

static Cd *cd = NULL;
static Track *track = NULL;
static Track *prev_track = NULL;
static Cdtext *cdtext = NULL;
static char *prev_filename = NULL;	/* last file in or before last track */
static char *cur_filename = NULL;	/* last file in the last track */
static char *new_filename = NULL;	/* last file in this track */
%}

%start cuefile

%union {
	long ival;
	char *sval;
}

%token <ival> NUMBER
%token <sval> STRING

/* global (header) */
%token CATALOG
%token CDTEXTFILE

%token FFILE
%token BINARY
%token MOTOROLA
%token AIFF
%token WAVE
%token MP3

/* track */
%token TRACK

%token <ival> AUDIO
%token <ival> MODE1_2048
%token <ival> MODE1_2352
%token <ival> MODE2_2336
%token <ival> MODE2_2048
%token <ival> MODE2_2342
%token <ival> MODE2_2332
%token <ival> MODE2_2352

/* ISRC is with CD_TEXT */
%token TRACK_ISRC

%token FLAGS
%token <ival> PRE
%token <ival> DCP
%token <ival> FOUR_CH
%token <ival> SCMS

%token PREGAP
%token INDEX
%token POSTGAP

/* CD-TEXT */
%token <ival> TITLE
%token <ival> PERFORMER
%token <ival> SONGWRITER
%token <ival> COMPOSER
%token <ival> ARRANGER
%token <ival> MESSAGE
%token <ival> DISC_ID
%token <ival> GENRE
%token <ival> TOC_INFO1
%token <ival> TOC_INFO2
%token <ival> UPC_EAN
%token <ival> ISRC
%token <ival> SIZE_INFO

%type <ival> track_mode
%type <ival> track_flag
%type <ival> time
%type <ival> cdtext_item

%%

cuefile
	: new_cd global_statements track_list
	;

new_cd
	: /* empty */ {
		cd = cd_init();
		cdtext = cd_get_cdtext(cd);
	}
	;

global_statements
	: /* empty */
	| global_statements global_statement
	;

global_statement
	: CATALOG STRING '\n' { cd_set_catalog(cd, $2); free($2); }
	| CDTEXTFILE STRING '\n' { free($2); /* ignored */ }
	| cdtext
	| track_data
	| error '\n'
	;

track_data
	: FFILE STRING file_format '\n' {
		if (NULL != new_filename) {
			yyerror("too many files specified\n");
			free(new_filename);
		}
		new_filename = $2; /*strdup($2);*/
	}
	;

track_list
	: track
	| track_list track
	;

track
	: new_track track_def track_statements
	;

file_format
	: BINARY
	| MOTOROLA
	| AIFF
	| WAVE
	| MP3
	;

new_track
	: /*empty */ {
		/* save previous track, to later set length */
		prev_track = track;

		track = cd_add_track(cd);
		cdtext = track_get_cdtext(track);

		if (cur_filename) free(cur_filename);
		cur_filename = new_filename ? strdup(new_filename) : 0;
		if (NULL != cur_filename) {
			if (prev_filename) free(prev_filename);
			prev_filename = strdup(cur_filename);
		}

		if (NULL == prev_filename)
			yyerror("no file specified for track");
		else
			track_set_filename(track, prev_filename);

		if (new_filename) free(new_filename);
		new_filename = NULL;
	}
	;

track_def
	: TRACK NUMBER track_mode '\n' {
		track_set_mode(track, $3);
	}
	;

track_mode
	: AUDIO
	| MODE1_2048
	| MODE1_2352
	| MODE2_2336
	| MODE2_2048
	| MODE2_2342
	| MODE2_2332
	| MODE2_2352
	;

track_statements
	: track_statement
	| track_statements track_statement
	;

track_statement
	: cdtext
	| FLAGS track_flags '\n'
	| TRACK_ISRC STRING '\n' { track_set_isrc(track, $2); free($2); }
	| PREGAP time '\n' { track_set_zero_pre(track, $2); }
	| INDEX NUMBER time '\n' {
		int i = track_get_nindex(track);
		long prev_length;

		if (0 == i) {
			/* first index */
			track_set_start(track, $3);

			if (NULL != prev_track && NULL == cur_filename) {
				/* track shares file with previous track */
				prev_length = $3 - track_get_start(prev_track);
				track_set_length(prev_track, prev_length);
			}
		}

		for (; i <= $2; i++)
			track_add_index(track, \
			track_get_zero_pre(track) + $3 \
			- track_get_start(track));
	}
	| POSTGAP time '\n' { track_set_zero_post(track, $2); }
	| track_data
	| error '\n'
	;

track_flags
	: /* empty */
	| track_flags track_flag { track_set_flag(track, $2); }
	;

track_flag
	: PRE
	| DCP
	| FOUR_CH
	| SCMS
	;

cdtext
	: cdtext_item STRING '\n' { cdtext_set ($1, $2, cdtext); free($2); }
	;

cdtext_item
	: TITLE
	| PERFORMER
	| SONGWRITER
	| COMPOSER
	| ARRANGER
	| MESSAGE
	| DISC_ID
	| GENRE
	| TOC_INFO1
	| TOC_INFO2
	| UPC_EAN
	| ISRC
	| SIZE_INFO
	;

time
	: NUMBER
	| NUMBER ':' NUMBER ':' NUMBER { $$ = time_msf_to_frame($1, $3, $5); }
	;

%%

/* lexer interface */
extern int cue_lineno;
extern int yydebug;
extern FILE *cue_yyin;

void yyerror (char *s)
{
	fprintf(stderr, "%d: %s\n", cue_lineno, s);
}

Cd *cue_parse (FILE *fp)
{
	cue_yyin = fp;
	yydebug = 0;

	int error = yyparse();
	cue_yylex_destroy();
	if (prev_filename) {
		free(prev_filename);
		prev_filename = 0;
	}
	if (cur_filename) {
		free(cur_filename);
		cur_filename = 0;
	}
	if (new_filename) {
		free(new_filename);
		new_filename = 0;
	}

	Cd *res = cd;
	if (error) {
		cd_delete(cd);
		res = 0;
	}
	cd = 0;
	track = 0;
	prev_track = 0;
	cdtext = 0;
	return res;
}

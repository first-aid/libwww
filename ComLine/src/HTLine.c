/*         		    					     HTLine.c
**	W3C COMMAND LINE TOOL
**
**	(c) COPRIGHT MIT 1995.
**	Please first read the full copyright statement in the file COPYRIGH.
**
**  Authors:
**	HFN		Henrik Frystyk Nielsen, (frystyk@w3.org)
**
**  History:
**	Nov 24 95	First version
*/

#include "WWWLib.h"			      /* Global Library Include file */
#include "WWWApp.h"
#include "WWWMIME.h"				    /* MIME parser/generator */
#include "WWWHTML.h"				    /* HTML parser/generator */
#include "WWWNews.h"				       /* News access module */
#include "WWWHTTP.h"				       /* HTTP access module */
#include "WWWFTP.h"
#include "WWWFile.h"
#include "WWWGophe.h"
#include "WWWStream.h"
#include "WWWTrans.h"
#include "WWWInit.h"

#include "HTLine.h"			     		 /* Implemented here */

#ifdef LIBWWW_SHARED
#include "HTextImp.h"
#endif

#ifndef W3C_VERSION
#define W3C_VERSION		"unspecified"
#endif

#define APP_NAME		"W3CCommandLine"
#define APP_VERSION		W3C_VERSION

/* Default page for "-help" command line option */
#define HELP	"http://www.w3.org/ComLine/User/CommandLine.html"

#define DEFAULT_OUTPUT_FILE	"w3c.out"
#define DEFAULT_RULE_FILE	"w3c.conf"
#define DEFAULT_LOG_FILE       	"w3c.log"

#define DEFAULT_TIMEOUT		10000		       /* timeout in millis */

#define DEFAULT_HOPS		0

#define DEFAULT_FORMAT		WWW_SOURCE

#if defined(__svr4__)
#define CATCH_SIG
#endif

typedef enum _CLFlags {
    CL_FILTER	= 0x1,
    CL_COUNT	= 0x2,
    CL_QUIET	= 0x4
} CLFlags;

#define SHOW_MSG		(!(cl->flags & CL_QUIET))

typedef struct _ComLine {
    HTRequest *		request;
    HTParentAnchor *	anchor;
    HTParentAnchor *	dest;			 /* Destination for PUT etc. */
    int 		timer;				/* Timeout on socket */
    char *		cwd;				  /* Current dir URL */
    char *		rules;
    char *		logfile;
    HTLog *		log;
    char *		outputfile;
    FILE *	        output;
    HTFormat		format;		        /* Input format from console */
    CLFlags		flags;
} ComLine;
	
/* ------------------------------------------------------------------------- */

/*	Standard (non-error) Output
**	---------------------------
*/
PUBLIC int OutputData(const char  * fmt, ...)
{
    int ret;
    va_list pArgs;
    va_start(pArgs, fmt);
    ret = vfprintf(stdout, fmt, pArgs);
    va_end(pArgs);
    return ret;
}

/* ------------------------------------------------------------------------- */

/*	Create a Command Line Object
**	----------------------------
*/
PRIVATE ComLine * ComLine_new (void)
{
    ComLine * me;
    if ((me = (ComLine *) HT_CALLOC(1, sizeof(ComLine))) == NULL)
	HT_OUTOFMEM("ComLine_new");
    me->timer = DEFAULT_TIMEOUT;
    me->cwd = HTGetCurrentDirectoryURL();
    me->output = OUTPUT;

    /* Bind the ConLine object together with the Request Object */
    me->request = HTRequest_new();
    HTRequest_setOutputFormat(me->request, DEFAULT_FORMAT);
    HTRequest_setContext (me->request, me);
    return me;
}

/*	Delete a Command Line Object
**	----------------------------
*/
PRIVATE BOOL ComLine_delete (ComLine * me)
{
    if (me) {
	HTRequest_delete(me->request);
	if (me->log) HTLog_close(me->log);
	if (me->output && me->output != STDOUT) fclose(me->output);
	HT_FREE(me->cwd);
	HT_FREE(me);
	return YES;
    }
    return NO;
}

PRIVATE void Cleanup (ComLine * me, int status)
{
    ComLine_delete(me);
    HTProfile_delete();
#ifdef VMS
    exit(status ? status : 1);
#else
    exit(status ? status : 0);
#endif
}

#ifdef CATCH_SIG
#include <signal.h>
/*								    SetSignal
**  This function sets up signal handlers. This might not be necessary to
**  call if the application has its own handlers (lossage on SVR4)
*/
PRIVATE void SetSignal (void)
{
    /* On some systems (SYSV) it is necessary to catch the SIGPIPE signal
    ** when attemting to connect to a remote host where you normally should
    ** get `connection refused' back
    */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
	if (PROT_TRACE) HTTrace("HTSignal.... Can't catch SIGPIPE\n");
    } else {
	if (PROT_TRACE) HTTrace("HTSignal.... Ignoring SIGPIPE\n");
    }
}
#endif /* CATCH_SIG */

PRIVATE void VersionInfo (void)
{
    OutputData("\n\nW3C Reference Software\n\n");
    OutputData("\tW3C Command Line Tool (%s) version %s.\n",
	     APP_NAME, APP_VERSION);
    OutputData("\tW3C Reference Library version %s.\n\n",HTLib_version());
    OutputData("Please send feedback to <libwww@w3.org>\n");
}

/*	terminate_handler
**	-----------------
**	This function is registered to handle the result of the request
*/
PRIVATE int terminate_handler (HTRequest * request, HTResponse * response,
			       void * param, int status) 
{
    ComLine * cl = (ComLine *) HTRequest_context(request);
    if (status == HT_LOADED) {
	if (cl) {
	    if (cl->flags & CL_COUNT) {
		OutputData("Content Length found to be %ld\n",
			 HTAnchor_length(cl->anchor));
	    }
	}
    } else {
	HTAlertCallback *cbf = HTAlert_find(HT_A_MESSAGE);
	if (cbf) (*cbf)(request, HT_A_MESSAGE, HT_MSG_NULL, NULL,
			HTRequest_error(request), NULL);
    }
    Cleanup(cl, status == HT_LOADED ? 0 : -1);
    return HT_OK;
}

PRIVATE int LineTrace (const char * fmt, va_list pArgs)
{
    return (vfprintf(stderr, fmt, pArgs));
}

/* ------------------------------------------------------------------------- */
/*				  MAIN PROGRAM				     */
/* ------------------------------------------------------------------------- */

int main (int argc, char ** argv)
{
    int		status = 0;	
    int		arg;
    int		tokencount = 0;
    BOOL	formdata = NO;
    HTChunk *	keywords = NULL;			/* From command line */
    HTAssocList*formfields = NULL;
    HTMethod	method = METHOD_GET;			    /* Default value */
    ComLine *	cl = ComLine_new();

    /* Starts Mac GUSI socket library */
#ifdef GUSI
    GUSISetup(GUSIwithSIOUXSockets);
    GUSISetup(GUSIwithInternetSockets);
#endif

#ifdef __MWERKS__ /* STR */
    InitGraf((Ptr) &qd.thePort); 
    InitFonts(); 
    InitWindows(); 
    InitMenus(); TEInit(); 
    InitDialogs(nil); 
    InitCursor();
    SIOUXSettings.asktosaveonclose = false;
    argc=ccommand(&argv);
#endif

    /* Initiate W3C Reference Library with a client profile */
    HTProfile_newNoCacheClient(APP_NAME, APP_VERSION);
    HTTrace_setCallback(LineTrace);

    /* Add our own filter to update the history list */
    HTNet_addAfter(terminate_handler, NULL, NULL, HT_ALL, HT_FILTER_LAST);

    /* Scan command Line for parameters */
    for (arg=1; arg<argc; arg++) {
	if (*argv[arg] == '-') {
	    
	    /* - alone => filter */
	    if (argv[arg][1] == '\0') {
		cl->flags |= CL_FILTER;	   
	    
	    /* -? or -help: show the command line help page */
	    } else if (!strcmp(argv[arg],"-?") || !strcmp(argv[arg],"-help")) {
		cl->anchor = (HTParentAnchor *) HTAnchor_findAddress(HELP);
		tokencount = 1;

	    /* non-interactive */
	    } else if (!strcmp(argv[arg], "-n")) {
		HTAlert_setInteractive(NO);

	    /* Treat the keywords as form data with a <name> "=" <value> */
	    } else if (!strcmp(argv[arg], "-form")) {
		formdata = YES;

	    /* from -- Initial represntation (only with filter) */
	    } else if (!strcmp(argv[arg], "-from")) {
		cl->format = (arg+1 < argc && *argv[arg+1] != '-') ?
		    HTAtom_for(argv[++arg]) : WWW_HTML;

	    /* to -- Final representation */
	    } else if (!strcmp(argv[arg], "-to")) {
		HTFormat format = (arg+1 < argc && *argv[arg+1] != '-') ?
		    HTAtom_for(argv[++arg]) : DEFAULT_FORMAT;
		HTRequest_setOutputFormat(cl->request, format);

	    /* destination for PUT, POST etc. */
	    } else if (!strcmp(argv[arg], "-dest")) {
		if (arg+1 < argc && *argv[arg+1] != '-') {
		    char * dest = HTParse(argv[++arg], cl->cwd, PARSE_ALL);
		    cl->dest = (HTParentAnchor *) HTAnchor_findAddress(dest);
		    HT_FREE(dest);
		}

	    /* source please */
	    } else if (!strcmp(argv[arg], "-source")) {
		HTRequest_setOutputFormat(cl->request, WWW_RAW);

	    /* log file */
	    } else if (!strcmp(argv[arg], "-l")) {
		cl->logfile = (arg+1 < argc && *argv[arg+1] != '-') ?
		    argv[++arg] : DEFAULT_LOG_FILE;

	    /* Max forward hops in case of TRACE request */
	    } else if (!strcmp(argv[arg], "-hops") ||
		       !strcmp(argv[arg], "-maxforwards")) {
		int hops = (arg+1 < argc && *argv[arg+1] != '-') ?
		    atoi(argv[++arg]) : DEFAULT_HOPS;
		if (hops >= 0) HTRequest_setMaxForwards(cl->request, hops);

	    /* rule file */
	    } else if (!strcmp(argv[arg], "-r")) {
		cl->rules = (arg+1 < argc && *argv[arg+1] != '-') ?
		    argv[++arg] : DEFAULT_RULE_FILE;

	    /* output filename */
	    } else if (!strcmp(argv[arg], "-o")) { 
		cl->outputfile = (arg+1 < argc && *argv[arg+1] != '-') ?
		    argv[++arg] : DEFAULT_OUTPUT_FILE;

	    /* timeout -- Change the default request timeout */
	    } else if (!strcmp(argv[arg], "-timeout")) {
		int timeout = (arg+1 < argc && *argv[arg+1] != '-') ?
		    atoi(argv[++arg]) : DEFAULT_TIMEOUT;
		if (timeout > 0) cl->timer = timeout;

	    /* preemptive or non-preemptive access */
	    } else if (!strcmp(argv[arg], "-single")) {
		HTRequest_setPreemptive(cl->request, YES);

	    /* content Length Counter */
	    } else if (!strcmp(argv[arg], "-cl")) { 
		cl->flags |= CL_COUNT;

	    /* print version and exit */
	    } else if (!strcmp(argv[arg], "-version")) { 
		VersionInfo();
		Cleanup(cl, 0);

	    /* run in quiet mode */
	    } else if (!strcmp(argv[arg], "-q")) { 
		cl->flags |= CL_QUIET;

#ifdef WWWTRACE
	    /* trace flags */
	    } else if (!strncmp(argv[arg], "-v", 2)) {
		HTSetTraceMessageMask(argv[arg]+2);
#endif

	    /* GET method */
	    } else if (!strcasecomp(argv[arg], "-get")) {
		method = METHOD_GET;

	    /* HEAD method */
	    } else if (!strcasecomp(argv[arg], "-head")) {
		method = METHOD_HEAD;

	    /* DELETE method */
	    } else if (!strcasecomp(argv[arg], "-delete")) {
		method = METHOD_DELETE;

	    /* POST Method */
	    } else if (!strcasecomp(argv[arg], "-post")) {
		method = METHOD_POST;

	    /* PUT Method */
	    } else if (!strcasecomp(argv[arg], "-put")) {
		method = METHOD_PUT;

	    /* OPTIONS Method */
	    } else if (!strcasecomp(argv[arg], "-options")) {
		method = METHOD_OPTIONS;

	    /* TRACE Method */
	    } else if (!strcasecomp(argv[arg], "-trace")) {
		method = METHOD_TRACE;

	    } else {
		if (SHOW_MSG) HTTrace("Bad Argument (%s)\n", argv[arg]);
	    }
	} else {	 /* If no leading `-' then check for URL or keywords */
    	    if (!tokencount) {
		char * ref = HTParse(argv[arg], cl->cwd, PARSE_ALL);
		cl->anchor = (HTParentAnchor *) HTAnchor_findAddress(ref);
		tokencount = 1;
		HT_FREE(ref);
	    } else if (formdata) {		   /* Keywords are form data */
		char * string = argv[arg];
		char * name = HTNextField(&string);
		char * value = HTNextField(&string);
		if (tokencount++ <= 1) formfields = HTAssocList_new();
		if (name && value) {
		    char * escaped_name = HTEscape(name, URL_XALPHAS);
		    char * escaped_value = HTEscape(value, URL_XALPHAS);
		    HTAssocList_addObject(formfields,
					  escaped_name, escaped_value);
		    HT_FREE(escaped_name);
		    HT_FREE(escaped_value);
		}
	    } else {		   	       /* keywords are search tokens */
		char * escaped = HTEscape(argv[arg], URL_XALPHAS);
		if (tokencount++ <= 1)
		    keywords = HTChunk_new(128);
		else
		    HTChunk_putc(keywords, ' ');
		HTChunk_puts(keywords, HTStrip(escaped));
		HT_FREE(escaped);
	    }
	}
    }

#ifdef CATCH_SIG
    SetSignal();
#endif

    if (!tokencount && !cl->flags & CL_FILTER) {
	if (SHOW_MSG) HTTrace("No URL specified\n");
	Cleanup(cl, -1);
    }

    /* Add progress notification */
    if (cl->flags & CL_QUIET) {
	HTList * global = HTAlert_global();
	HTAlertCall_delete(global, HTProgress);
    }

    /* Output file specified? */
    if (cl->outputfile) {
	if ((cl->output = fopen(cl->outputfile, "wb")) == NULL) {
	    if (SHOW_MSG) HTTrace("Can't open `%s'\\n",cl->outputfile);
	    cl->output = OUTPUT;
	}
    }

    /*
    ** Set up the output. Even though we don't use this explicit, it is
    ** required in order to show the stream stack that we know that we are
    ** getting raw data output on the output stream of the request object.
    */
    HTRequest_setOutputStream(cl->request,
			      HTFWriter_new(cl->request, cl->output, YES));

    /* Setting event timeout */
    HTHost_setEventTimeout(cl->timer);

    /*
    ** Make sure that the first request is flushed immediately and not
    ** buffered in the output buffer
    */
    HTRequest_setFlush(cl->request, YES);

    /* Log file specifed? */
    if (cl->logfile) {
	cl->log = HTLog_open(cl->logfile, YES, YES);
        if (cl->log) HTNet_addAfter(HTLogFilter, NULL, cl->log, HT_ALL, HT_FILTER_LATE);
    }

    /* Just convert formats */
    if (cl->flags & CL_FILTER) {
#ifdef STDIN_FILENO
	HTRequest_setAnchor(cl->request, (HTAnchor *) cl->anchor);
	HTRequest_setPreemptive(cl->request, YES);
	HTLoadSocket(STDIN_FILENO, cl->request);
#endif
	Cleanup(cl, 0);
    }
    
    /* Content Length Counter */
    if (cl->flags & CL_COUNT) {
	HTRequest_setOutputStream(cl->request,
				  HTContentCounter(HTBlackHole(),
						   cl->request, 0x2000));
    }

    /* Rule file specified? */
    if (cl->rules) {
	char * rules = HTParse(cl->rules, cl->cwd, PARSE_ALL);
	if (!HTLoadRules(rules))
	    if (SHOW_MSG) HTTrace("Can't access rules\n");
	HT_FREE(rules);
    }

    /* Start the request */
    switch (method) {
    case METHOD_GET:

	if (formdata)
	    status = HTGetFormAnchor(formfields, (HTAnchor *) cl->anchor,
				     cl->request);
	else if (keywords)
	    status = HTSearchAnchor(keywords, (HTAnchor *) cl->anchor,
				    cl->request);
	else
	    status = HTLoadAnchor((HTAnchor *) cl->anchor, cl->request);
	break;

    case METHOD_HEAD:
	if (formdata) {
	    HTRequest_setMethod(cl->request, METHOD_HEAD);
	    status = HTGetFormAnchor(formfields, (HTAnchor *) cl->anchor,
				     cl->request);
	} else if (keywords) {
	    HTRequest_setMethod(cl->request, METHOD_HEAD);
	    status = HTSearchAnchor(keywords, (HTAnchor *) cl->anchor,
				    cl->request);
	} else
	    status = HTHeadAnchor((HTAnchor *) cl->anchor, cl->request);
	break;

    case METHOD_DELETE:
	status = HTDeleteAnchor((HTAnchor *) cl->anchor, cl->request);
	break;

    case METHOD_POST:
	if (formdata) {
	    HTParentAnchor * posted = NULL;
	    posted = HTPostFormAnchor(formfields, (HTAnchor *) cl->anchor,
				      cl->request);
	    status = posted ? YES : NO;
	} else {

	    /* MORE */

	    status = NO;	    
	}
	break;

    case METHOD_PUT:
	status = HTPutDocumentAnchor(cl->anchor, (HTAnchor *) cl->dest,
				     cl->request);
	break;

    case METHOD_OPTIONS:
	status = HTOptionsAnchor((HTAnchor *) cl->anchor, cl->request);
	break;	

    case METHOD_TRACE:
	status = HTTraceAnchor((HTAnchor *) cl->anchor, cl->request);
	break;	

    default:
	if (SHOW_MSG) HTTrace("Don't know this method\n");
	break;
    }

    if (keywords) HTChunk_delete(keywords);
    if (formfields) HTAssocList_delete(formfields);
    if (status != YES) {
	if (SHOW_MSG) HTTrace("Can't access resource\n");
	Cleanup(cl, -1);
    }

    /* Go into the event loop... */
    HTEventList_loop(cl->request);

    /* Only gets here if event loop fails */
    Cleanup(cl, 0);
    return 0;
}

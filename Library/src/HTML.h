/*  */

/*              The HTML to rtf object converter                        HTML.h
**              --------------------------------
*/

#ifndef HTML_H
#define HTML_H

#include "HTUtils.h"
#include "HTAnchor.h"
#include "HTMLDTD.h"


extern CONST HTStructuredClass HTMLPresentation;

/*      HTConverter to present HTML
*/
PUBLIC HTStream* HTMLToPlain PARAMS((
        HTPresentation *        pres,
        HTParentAnchor *        anchor,
        HTStream *              sink));

PUBLIC HTStream* HTMLToC PARAMS((
        HTPresentation *        pres,
        HTParentAnchor *        anchor,
        HTStream *              sink));

PUBLIC HTStream* HTMLPresent PARAMS((
        HTPresentation *        pres,
        HTParentAnchor *        anchor,
        HTStream *              sink));

extern HTStructured* HTML_new PARAMS((
        HTParentAnchor * anchor,
        HTFormat        format_out,
        HTStream *      target));

/*      Names for selected internal representations:
*/
typedef enum _HTMLCharacterSet {
        HTML_ISO_LATIN1,
        HTML_NEXT_CHARS,
        HTML_PC_CP950
} HTMLCharacterSet;

extern void HTMLUseCharacterSet PARAMS((HTMLCharacterSet i));

/*      Record error message as a hypertext object
**      ------------------------------------------
**
**      The error message should be marked as an error so that
**      it can be reloaded later.
**      This implementation just throws up an error message
**      and leaves the document unloaded.
**
** On entry,
**      sink    is a stream to the output device if any
**      number  is the HTTP error number
**      message is the human readable message.
** On exit,
**      a retrun code like HT_LOADED if object exists else < 0
*/

PUBLIC int HTLoadError PARAMS((
        HTStream *      sink,
        int             number,
        CONST char *    message));

#endif
/*

    */

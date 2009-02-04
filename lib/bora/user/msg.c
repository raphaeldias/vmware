/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * msg.c --
 *
 *   User interaction through modeless messages and modal dialogs.
 */


#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#  include <winerror.h>
#endif

#include "vmware.h"
#include "productState.h"
#ifdef _WIN32
#  include "win32util.h"
#  include "win32u.h"
#endif
#include "util.h"
#include "str.h"
#include "config.h"
#include "msg.h"
#include "vm_version.h"
#include "dynbuf.h"
#include "localconfig.h"
#include "dictionary.h"

#define LOGLEVEL_MODULE main
#include "loglevel_user.h"


/*
 * Constants
 */

#define MSG_MAX_ID	128


/*
 * Global data
 */

Msg_String const Msg_YesNoButtons[] = {
   {BUTTONID(yes)  "_Yes"},
   {BUTTONID(no) "_No"},
   {NULL}
};

Msg_String const Msg_OKButtons[] = {
   {BUTTONID(ok)  "OK"},
   {NULL}
};

Msg_String const Msg_RetryCancelButtons[] = {
   {BUTTONID(retry)  "_Retry"},
   {BUTTONID(cancel) "Cancel"},
   {NULL}
};

Msg_String const Msg_OKCancelButtons[] = {
   {BUTTONID(ok)  "OK"},
   {BUTTONID(cancel) "Cancel"},
   {NULL}
};

Msg_String const Msg_RetryAbortButtons[] = {
   {BUTTONID(retry) "_Retry"},
   {BUTTONID(abort) "_Abort"},
   {NULL}
};

Msg_String const Msg_Severities[MSG_NUM_SEVERITIES]  = {
   { MSGID(msg.info) "Information" },
   { MSGID(msg.info) "Information" },	// XXX won't timeout
   { MSGID(msg.warning) "Warning" },
   { MSGID(msg.error) "Error" },
   { MSGID(msg.configEditor) "Configuration editor" },
   { MSGID(msg.getLicenseError) "Get license error" },
   { MSGID(msg.extendLicenseError) "Extend license error" },
   { MSGID(msg.extendLicenseInfo) "Extend license info" },
   { MSGID(msg.homePageInfo) "Home page info" },
};


/*
 * Local functions
 */

static Msg_List *MsgAppend(const char *idFmt, va_list args);
static void MsgPost(MsgSeverity severity, const char *id);
static unsigned MsgQuestion(Msg_String const *buttons, int defaultAnswer,
                            const char *idFmt, va_list args);
static HintResult MsgHint(Bool defaultShow, HintOptions options,
			  Msg_List *m);
static Bool MsgIsQuestionAnswered(Msg_String const *buttons, int defaultAnswer,
                                  const char *id, unsigned int *reply);
static void MsgPostStderr(MsgSeverity severity,
                          const char *msgID, const char *msg);
static int MsgQuestionStdio(char const * const *names, int defaultAnswer,
                            const char *msgID, const char *text);
static int MsgProgressStdio(const char *msgID, const char *message,
                            int percent, Bool cancelButton);
static HintResult MsgHintStdio(HintOptions options,
                               const char *msgID, const char *message);
static const char *MsgGetString(const char *idString, Bool noLocalize,
				char *id);
static const char *MsgLookUpString(Dictionary *dict, const char *id);
static char *MsgStripMnemonic(const char *localizedString);

static void MsgLogList(const char *who, const char *label, Msg_List *messages);
static void MsgLocalizeList(Msg_List *messages, DynBuf *b);
static char *MsgLocalizeList1(Msg_List *m, size_t *len);
static char *MsgErrno2LocalString(Err_Number errorNumber,
		                  MsgFmt_ArgPlatform platform,
		                  const char *englishString);
#ifdef XXX_MSGFMT_DEBUG
static void MsgPrintMsgFmt(const char *format, MsgFmt_Arg *args, int numArgs);
#endif


/*
 * Local data
 */

typedef struct MsgState { 
   MsgCallback callback;
   Msg_List *head;
   Msg_List **tailp;
   char *locale;		/* locale name */
   Dictionary *dict;		/* message dictionary */
   const char *severities[MSG_NUM_SEVERITIES];
} MsgState;

static MsgState *msgState;


/*
 *----------------------------------------------------------------------
 *
 * MsgGetState --
 *
 *      Get the msg state corresponding to the current thread.
 *
 * Results:
 *      the thread's msg state.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static INLINE MsgState *
MsgGetState(void)
{
   static MsgState initMsgState = {
      {  
	 MsgPostStderr,
	 MsgQuestionStdio,
	 MsgProgressStdio,
	 MsgHintStdio,
      }
   };

   if (msgState == NULL) {
      msgState = Util_SafeMalloc(sizeof *msgState);
      *msgState = initMsgState;
      msgState->tailp = &msgState->head;
   }
   return msgState;
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgAppend --
 *
 *      Appends a possibly partial error message to
 *	a buffer in this thread.  If we're running on
 *	behalf of a different thread, append to that
 *	buffer.
 *
 * Results:
 *      Message ID in id (if not NULL).
 *	New Msg_List structure.
 *
 * Side effects:
 *	Add stuff to buffer.
 *
 * Non-side effects:
 *      Don't report message to user yet.
 *
 *-----------------------------------------------------------------------------
 */

static Msg_List *
MsgAppend(const char *idFmt,	// IN message ID and English message
          va_list args)		// IN args
{
   MsgState *state = MsgGetState();
   Msg_List *m = Msg_VCreateMsgList(idFmt, args);

   *state->tailp = m;
   state->tailp = &m->next;
   return m;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_VCreateMsgList --
 *
 *     Create the Msg_List item from the message with va_list.
 *
 * Results:
 *	New Msg_List structure.
 *
 * Side effects:
 *	Callers are responsible to free the returned Msg_list.
 *
 *-----------------------------------------------------------------------------
 */

Msg_List *
Msg_VCreateMsgList(const char *idFmt,	// IN message ID and English message
                va_list args)		// IN args
{
   char idBuf[MSG_MAX_ID];
   Msg_List *m;
   const char *format;
   Bool status;
   char *error;

   format = MsgGetString(idFmt, TRUE, idBuf);
   m = Util_SafeMalloc(sizeof *m);
   m->format = Util_SafeStrdup(format);
   ASSERT(Str_Strncasecmp(idBuf, "msg.", 4) == 0);
   m->id = Util_SafeStrdup(idBuf);
   status = MsgFmt_GetArgs(format, args, &m->args, &m->numArgs, &error);
   if (!status) {
      Log("Msg_VCreateMsgList error: %s\nformat <%s>", error, format);
      PANIC();
   }

   m->next = NULL;
   return m;
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_Append --
 *
 *      append the message to the uncommited error message buffer.
 *      message will be finally displayed with the next Msg_Post()
 *      or deleted at the next Msg_Reset()
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Append to buffer.
 *
 *----------------------------------------------------------------------
 */

void 
Msg_Append(const char *idFmt,	// IN: message ID and English message
           ...)			// IN: additonal args
{
   va_list args;

   va_start(args, idFmt);
   MsgAppend(idFmt, args);
   va_end(args);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_Post --
 *
 *      post all previously uncommitted message, as well as this 
 *      message into a non-modal window
 *
 *	The message is broken at newlines in the string.
 *	(This holds also for Msg_Append and Msg_Question).
 *	The final newline is ignored.
 *
 *	As a matter of convention, please put a period at
 *	the end of the message.
 *
 *	Note: each MSGID passed to Msg_Post or Msg_Append needs to be unique.  The
 *	IDs are used as keys to look up translations in the translation table of
 *	message strings.   Additionally, MSGIDs should not be recycled to mean 
 *	anything different than their original usage.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Report to user and wait for user acknowledgement.
 *
 *----------------------------------------------------------------------
 */

void
Msg_Post(MsgSeverity severity, const char *idFmt, ...)
{
   va_list args;
   Msg_List *msg;

   va_start(args, idFmt);
   msg = MsgAppend(idFmt, args);
   va_end(args);

   MsgPost(severity, msg->id);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_PostMsgList --
 *
 *      This is same as Msg_Post except it takes Msg_List as
 *      argument. The caller should not free msg->args and
 *      it will be freed when MsgPost is called.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Report to user and wait for user acknowledgement.
 *
 *----------------------------------------------------------------------
 */

void
Msg_PostMsgList(MsgSeverity severity, Msg_List *msg)
{
   Msg_AppendMsgList(msg->id, msg->format, msg->args, msg->numArgs);
   MsgPost(severity, msg->id);
}


void
MsgPost(MsgSeverity severity, // IN: severity
        const char *id)       // IN: message ID
{
   MsgState *state = MsgGetState();
   Msg_List *list;
   Bool localize = FALSE;
   Bool warn = FALSE;
   Bool post = FALSE;

   ASSERT(severity < MSG_NUM_SEVERITIES);

   if (state->severities[0] == NULL) {
      int i;
      for (i = 0; i < MSG_NUM_SEVERITIES; i++) {
	 state->severities[i] =
	    MsgGetString(Msg_Severities[i].idFmt, TRUE, NULL);
      }
   }

   /*
    * We have to take the list off the state structure
    * because we seem to be inadvertently reentrant.
    */

   list = Msg_GetMsgListAndReset();

   MsgLogList("Msg_Post", state->severities[severity], list);

   /*
    * The logic of message disposition is complicated,
    * so first we figure out what to do and save that
    * in the three booleans, then we act on them.
    */

   // XXX unify config options that stuff msgs -- mginzton
   if (Config_GetBool(FALSE, "msg.autoAnswer")) {
   } else if (severity != MSG_ERROR && Config_GetBool(FALSE, "msg.noOK")) {
      if (!Config_GetBool(FALSE, "msg.noOKnoWarning")) {
	 localize = warn = TRUE;
      }
   } else {
      if (state->callback.post != NULL) {
	 localize = post = TRUE;
      }
      if (state->callback.postList != NULL) {
	 post = TRUE;
      }
   }

   if (post) {
      if (state->callback.postList != NULL) {
         state->callback.postList(severity, list);
      }
   }

   if (localize) {
      DynBuf b;
      DynBuf_Init(&b);
      MsgLocalizeList(list, &b);
      if (warn) {
	 Warning("MSG: %s\n", (char *) DynBuf_Get(&b));
      }
      if (post) {
         if (state->callback.post != NULL) {
            state->callback.post(severity, id, DynBuf_Get(&b));
         }
      }
      DynBuf_Destroy(&b);
   }

   Msg_FreeMsgList(list);
}


void
MsgPostStderr(MsgSeverity severity,   // IN
              const char *msgID,      // IN
              const char *msg)        // IN
{
#ifdef _WIN32
   char *severityTitle = Msg_GetString(Msg_Severities[severity].idFmt);
   char *title = Str_Asprintf(NULL, "%s %s", ProductState_GetName(), 
                              severityTitle);
   Win32U_MessageBox(NULL, (char *) msg, title, MB_OK);
   free(title);
   free(severityTitle);
#else
   fprintf(stderr,
      "\n"
      "%s %s:\n"
      "%s"  // msg is already newline terminated
      "\n",
      ProductState_GetName(),
      MsgGetString(Msg_Severities[severity].idFmt, FALSE, NULL),
      msg);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_Format --
 *
 *      Format a message and return it in an allocated buffer.
 *
 * Results:
 *      The formated message string.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_Format(const char *idFmt, // IN: message ID and English message
           ...)               // IN: args
{
   va_list arguments;
   char *formatted;

   va_start(arguments, idFmt);
   formatted = Msg_VFormat(idFmt, arguments);
   va_end(arguments);

   return formatted;
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_VFormat --
 *
 *      Format a message with va_list arguments and return it
 *	in an allocated buffer.
 *
 * Results:
 *      The formated message string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Msg_VFormat(const char *idFmt,	// IN: message ID and English message
            va_list arguments)	// IN: args
{
   return Str_SafeVasprintf(NULL, MsgGetString(idFmt, FALSE, NULL), arguments);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_GetMessages --
 *
 *      Return the accumulated messages.
 *
 * Results:
 *      Accumulated message.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Msg_GetMessages() 
{
   MsgState *state = MsgGetState();
   static DynBuf b;

   DynBuf_SetSize(&b, 0);
   MsgLocalizeList(state->head, &b);
   return DynBuf_Get(&b);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_Reset --
 *
 *      discard appended message since the last post
 *
 * Results:
 *      
 *      void
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void 
Msg_Reset(Bool log) 
{
   MsgState *state = MsgGetState();

   if (state->head != NULL) {
      Msg_List *list = state->head;
      state->head = NULL;
      state->tailp = &state->head;
      if (log) { 
	 MsgLogList("Msg_Reset", "", list);
      }
      Msg_FreeMsgList(list);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_GetMessagesAndReset --
 *
 *      Return content of message buffer and reset it.
 *
 * Results:
 *      Content of message buffer.
 *
 * Side effects:
 *	Reset message buffer.
 *
 *-----------------------------------------------------------------------------
 */

const char *
Msg_GetMessagesAndReset()
{
   Msg_List *list = Msg_GetMsgListAndReset();
   static DynBuf b;

   DynBuf_SetSize(&b, 0);
   MsgLocalizeList(list, &b);
   Msg_FreeMsgList(list);
   return DynBuf_Get(&b);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_Present --
 *
 *      tests if an appended but not commited message is present
 *
 * Results:
 *      
 *      TRUE is such a message is waiting for a Post/Reset
 *      
 *      
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool 
Msg_Present(void)
{
   return MsgGetState()->head != NULL;
}


#ifdef _WIN32
/*
 *----------------------------------------------------------------------
 *
 * Msg_HResult2String --
 *
 *      Return a string that corresponds to the passed Windows HRESULT
 *
 * Results:
 *      
 *      Error message string
 *      
 *
 * Side effects:
 *      None.
 *
 * Bugs:
 *	no internationalization support for now
 *	The result is not really a string, just a break down of the fields
 *	The result is a static buffer.
 *
 *----------------------------------------------------------------------
 */

const char *
Msg_HResult2String(HRESULT hr)
{
   static char buf[1024];

   Str_Sprintf(buf, sizeof buf, "HRESULT(0x%08x: sev %d fac %d code %d)",
               hr, HRESULT_SEVERITY(hr), HRESULT_FACILITY(hr), HRESULT_CODE(hr));
   return buf;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * Msg_Question --
 *
 *      modal dialog
 *
 * Results:
 *      Which button the user answered with.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

unsigned int 
Msg_Question(Msg_String const *buttons,
             int defaultAnswer,
	     const char *idFmt,
             ...)
{
   va_list args;
   unsigned int reply;

   va_start(args, idFmt);
   reply = MsgQuestion(buttons, defaultAnswer, idFmt, args);
   va_end(args);

   return reply;
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgIsQuestionAnswered --
 *
 *	Checks to see if a given id has an answer (i.e. the name
 *	of a button in the C locale) hardcoded in the appropriate
 *	config or preferences file.
 *
 * Results:
 *	TRUE if yes: '*reply' is set
 *	FALSE if no
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

static Bool
MsgIsQuestionAnswered(Msg_String const *buttons,// IN
                      int defaultAnswer,        // IN
                      const char *id,           // IN
                      unsigned int *reply)      // OUT
{
   char *answer = NULL;
   int i;

   /*
    * Sanity check
    */
   for (i = 0; i <= defaultAnswer; i++) {
      ASSERT(buttons[i].idFmt != NULL);
   }

   /*
    * Look for explicit answer.msg.id in config files
    */

   answer = Config_GetString(NULL, "answer.%s", id);
   if (answer) {
      for (i = 0; buttons[i].idFmt != NULL; i++) {
         if (Msg_CompareAnswer(buttons, i, answer) == 0) {
            Log("MsgIsQuestionAnswered: Using config default '%s' "
                "as the answer for '%s'\n",
	        answer, id);
            *reply = i;
            free(answer);
            return TRUE;
         }
      }
      free(answer);
   }

   /*
    * Can we apply default answer?
    */
   if (Config_GetBool(FALSE, "msg.autoAnswer")) {
      answer = Msg_GetString(buttons[defaultAnswer].idFmt);
      Log("MsgIsQuestionAnswered: Using builtin default '%s' "
          "as the answer for '%s'\n",
          answer, id);
      *reply = defaultAnswer;
      free(answer);
      return TRUE;
   }

   /*
    * No automatic answer; we'll have to resort to asking the user
    */
   return FALSE;
}


unsigned int 
MsgQuestion(Msg_String const *buttons,
            int defaultAnswer,
	    const char *idFmt,
            va_list args)
{
   MsgState *state = MsgGetState();
   unsigned int reply = 0;
   Msg_List *question;
   Msg_List *list;
   Msg_String const *startButtonList = buttons;

   ASSERT(idFmt != NULL); 
   ASSERT(buttons);

   question = MsgAppend(idFmt, args);

   /*
    * We have to take the list off the state structure
    * because we seem to be inadvertently reentrant.
    */

   list = Msg_GetMsgListAndReset();

   MsgLogList("Msg_Question", "", list);

   if (MsgIsQuestionAnswered(buttons, defaultAnswer, question->id, &reply)) {
      /*
       * We MsgAppend()ed above, so we need to clear the message buffer now.
       */
      Msg_Reset(FALSE);
   } else {

      if (state->callback.question != NULL) {
	 const char *names[MSG_QUESTION_MAX_BUTTONS];
	 DynBuf b;
	 unsigned int i;

	 for (i = 0; i < ARRAYSIZE(names) - 1 && buttons->idFmt != NULL;
	      i++, buttons++) {
	    names[i] = MsgGetString(buttons->idFmt, FALSE, NULL);
	 }
	 names[i] = NULL;

	 DynBuf_Init(&b);
	 MsgLocalizeList(list, &b);

	 reply = state->callback.question(names, defaultAnswer,
					  question->id, DynBuf_Get(&b));

	 DynBuf_Destroy(&b);
      }

      if (state->callback.questionList != NULL) {
	 reply = state->callback.questionList(startButtonList, defaultAnswer, list);
      }
   }

   Log("Msg_Question: %s reply=%d\n", question->id, reply);

   Msg_FreeMsgList(list);

   return reply;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_CompareAnswer --
 *
 *      Compare the answer returned by Msg_Question with the supplied string,
 *      ignoring case and mnemonic differences.
 *
 * Results:
 *      strcasecmp() of mnemonic-less answer and string.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int
Msg_CompareAnswer(Msg_String const *buttons,	// IN: possible answers
		  unsigned answer,		// IN: from Msg_Question()
		  const char *string)		// IN: text answer
{
   int result;
   const char *actualAnswer = MsgGetString(buttons[answer].idFmt, TRUE, NULL);
   char *actualAnswerStripped = MsgStripMnemonic(actualAnswer);
   char *targetAnswerStripped = MsgStripMnemonic(string);

   result = Str_Strcasecmp(actualAnswerStripped, targetAnswerStripped);

   free(actualAnswerStripped);
   free(targetAnswerStripped);

   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * MsgEatUntilNewLineStdio --
 *
 *      Eat characters until a new line is encountered
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stdin is consumed
 *
 *----------------------------------------------------------------------
 */

static void
MsgEatUntilNewLineStdio(void)
{
   do {
   } while (fgetc(stdin) != '\n');
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgQuestionStdio --
 *
 *    Choose an answer for a question on stdio
 *
 * Results:
 *    The number of the chosen answer
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

int 
MsgQuestionStdio(char const * const *names, // IN
                 int defaultAnswer,         // IN: Unused
                 const char *msgID,         // IN
                 const char *text)          // IN
{
   unsigned int i;
   char buf[1024];
   const char *ptr;
   unsigned int reply;
   char *question = Msg_GetString(MSGID(msg.question) "Question");
   char *chooseNumber =
      Msg_GetString(MSGID(msg.chooseNumber) "Please choose a number [0-%d]: ");
 
   printf("\n\n"
	  "%s %s:\n"
          "%s"
          "\n",
          ProductState_GetName(),
          question,
          text /* Newline terminated --hpreg */);

   fflush(stdout);

   for (i = 0; names[i]; i++) {
      printf("%d) %s\n", i, names[i]);
   }
   printf("\n");

again:
   printf(chooseNumber, i-1);
   fflush(stdout);

   if (fgets(buf, sizeof(buf), stdin) == NULL) {
      /* Unknown error */
      printf("\n"
             "\n");
      goto again;
   }
   printf("\n");

   /*
    * Scan the string by hand instead of using scanf(). I do this as a quick
    * workaround for shipstopper bug 7570 --hpreg
    */

   ptr = buf;

   while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r') {
      ptr++;
   }

   if (*ptr == '\n') {
      /* The user hit 'Enter' */
      goto again;
   }

   reply = 0;
   while (*ptr >= '0' && *ptr <= '9') {
      reply *= 10;
      reply += *ptr - '0';
      ptr++;
   }

   while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r') {
      ptr++;
   }

   if (*ptr != '\n') {
      goto again;
   }

   if (reply >= i) {
      goto again;
   }
   
   free(question);
   free(chooseNumber);
   return reply;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_Progress --
 *
 *      Display progress of long operation.
 *	When PERCENTDONE = -1, map the window.
 *	When 0 <= PERCENTDONE <= 100, update the progress bar.
 *	When PERCENTDONE = 101, unmap the window.
 *	ID, FMT, and ... are only meaningful when PERCENTDONE = -1.
 *
 * Results:
 *
 *   0 if the cancel button is not displayed.
 *   1 if the cancel button is displayed and selected.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int
Msg_Progress(int percentDone,		// -1 to start, 101 to stop
	     Bool cancelButton,
	     const char *idFmt,
	     ...)
{
   MsgState *state = MsgGetState();
   int result = 0;

   if (idFmt == NULL) {
      result = state->callback.progress(NULL, "", percentDone, cancelButton);
   } else {
      char id[MSG_MAX_ID];
      Msg_List ml;
      va_list va;
      Bool status;
      char *error;

      ml.format = (char *) MsgGetString(idFmt, TRUE, id);
      ml.id = (char *) id;
      va_start(va, idFmt);
      status = MsgFmt_GetArgs(ml.format, va, &ml.args, &ml.numArgs, &error);
      va_end(va);
      if (!status) {
	 Log("Msg_Progress error: %s\nformat <%s>", error, ml.format);
	 PANIC();
      }
      ml.next = NULL;

      if (state->callback.progress != NULL) {
	 char *formatted = MsgLocalizeList1(&ml, NULL);
	 result = state->callback.progress(ml.id, formatted,
					   percentDone, cancelButton);
	 free(formatted);
      }
      if (state->callback.progressList != NULL) {
	 result = state->callback.progressList(&ml, percentDone, cancelButton);
      }

      MsgFmt_FreeArgs(ml.args, ml.numArgs);
   }

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_LazyProgressStart --
 *
 *      Start reporting progress of long operation that doesn't block.
 *
 * Results:
 *
 *   A handle to pass to Msg_LazyProgress and Msg_LazyProgressEnd.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void *
Msg_LazyProgressStart(const char *idFmt,             // IN
                      ...)
{
   MsgState *state = MsgGetState();
   va_list va;
   char id[MSG_MAX_ID];
   void *handle = NULL;
   Msg_List ml;
   Bool status;
   char *error;

   ASSERT(state != NULL);
   ASSERT(idFmt);

   if (state->callback.lazyProgressStart == NULL &&
       state->callback.lazyProgressStartList == NULL) {
      /* Who cares? */
      return NULL;
   }

   ml.format = (char *) MsgGetString(idFmt, TRUE, id);
   ml.id = (char *) id;
   va_start(va, idFmt);
   status = MsgFmt_GetArgs(ml.format, va, &ml.args, &ml.numArgs, &error);
   va_end(va);
   if (!status) {
      Log("Msg_LazyProgressStart error: %s\nformat <%s>", error, ml.format);
      PANIC();
   }
   ml.next = NULL;

   if (state->callback.lazyProgressStart != NULL) {
      char *formatted = MsgLocalizeList1(&ml, NULL);
      handle = state->callback.lazyProgressStart(ml.id, formatted);
      free(formatted);
   }
   if (state->callback.lazyProgressStartList != NULL) {
      handle = state->callback.lazyProgressStartList(&ml);
   }

   MsgFmt_FreeArgs(ml.args, ml.numArgs);

   return handle;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_LazyProgress --
 *
 *      Update the amount of progress that has been made.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Msg_LazyProgress(void *handle,          // IN
                 int percent)           // IN
{
   MsgState *state = MsgGetState();

   if (!state->callback.lazyProgress) {
      /* Who cares? */
      return;
   }

   state->callback.lazyProgress(handle, percent);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_LazyProgressEnd --
 *
 *      End the reporting of lazy progress.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Msg_LazyProgressEnd(void *handle)               // IN
{
   MsgState *state = MsgGetState();

   if (!state->callback.lazyProgressEnd) {
      /* Who cares? */
      return;
   }

   state->callback.lazyProgressEnd(handle);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_ProgressScaled --
 *
 *   Wrapper to Msg_Progress which maps percentDone into a smaller range.
 *   Used by callers that display progress for an sub-operation that is
 *   part of larger operation. Assumes that the sub-operations are
 *   serialized and each sub-operation knows how many suboperations have 
 *   completed and what the total number of sub-operations is.
 *
 *   The net effect is that each sub-operation is responsible for updating
 *   a portion of a single progress bar.
 *
 *   Does not take any progress bar title string, as it is purely for updating
 *   existing progress bar.
 *
 * Results:
 *
 *   0 if the cancel button is not displayed.
 *   1 if the cancel button is displayed and selected.
 *
 * Side effects:
 *   None.
 *
 *-----------------------------------------------------------------------------
 */

int
Msg_ProgressScaled(int percentDone,
                   int opsDone,
                   int opsTotal,
                   Bool cancelButton)
{
   int result;
   int adjustedPercentDone, minPercent, maxPercent;

   ASSERT(opsTotal >= 0);

   if (opsTotal == 0) {
      adjustedPercentDone = percentDone;
   } else {
      ASSERT(opsDone >= 0);
      ASSERT(opsDone < opsTotal);
      minPercent = (opsDone * 100) / opsTotal;
      maxPercent = ((opsDone+1) * 100) / opsTotal;

      if (percentDone == MSG_PROGRESS_START || 
          percentDone == MSG_PROGRESS_STOP) {
         /*
          * Starting/Stopping progress is not the responsibility of
          * any sub-operation but the job of the initiator of the
          * full operation.
          */
         return 0;
      }
      adjustedPercentDone = 
         minPercent + ((maxPercent - minPercent) * percentDone) / 100;
   }

   result = Msg_Progress(adjustedPercentDone, cancelButton, NULL);
   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * MsgProgressStdio --
 *
 *   Progress callback for text mode applications
 *
 * Results:
 *
 *   0 if the cancel button is not displayed.
 *   1 if the cancel button is displayed and selected.
 *
 * Side effects:
 *   None.
 *
 *----------------------------------------------------------------------
 */

#define NUM_CHAR_PER_LINE 79
static int
MsgProgressStdio(const char *msgID,
                 const char *message, // IN: Message to display
		 int percent,         // IN: Completion percentage
		 Bool cancelButton)   // IN: display cancel button
{
   int cancel = 0;
   char buf[NUM_CHAR_PER_LINE + 1];
   static char msg[NUM_CHAR_PER_LINE + 1];

   if (percent < 0) {
      Str_Snprintf(msg, sizeof msg, "%s", message);
   }

   Str_Snprintf(buf, sizeof buf, "%.*s (%d%%)",
                (int) sizeof buf - 16, msg, percent);
   printf("\r%-*.*s", NUM_CHAR_PER_LINE, NUM_CHAR_PER_LINE, buf);
   fflush(stdout);

   if (percent > 100) { /* Done */
      printf("\n");
   }

   return cancel;
}
#undef NUM_CHAR_PER_LINE


/*
 *----------------------------------------------------------------------
 *
 * Msg_Hint --
 *
 *      Display a hint for the user.  If defaultShow is FALSE,
 *	don't show the hint unless hint.<id> is TRUE.
 *
 *	Newlines in hints cause line breaks in the message.
 *	Lines are also broken as needed.
 *
 * Results:
 *	Returns HINT_CONTINUE, HINT_CANCEL or HINT_NOT_SHOWN.
 *
 * Side effects:
 *      The user may disable future occurrences of the hint
 *	after viewing it.
 *
 *----------------------------------------------------------------------
 */

HintResult
Msg_Hint(Bool defaultShow,	// IN: default enabling
	 HintOptions options,	// IN: options
	 const char *idFmt,	// IN: message ID and English message format
	 ...)			// IN: format arguments
{
   va_list args;
   HintResult result;
   Msg_List *m;

   va_start(args, idFmt);
   m = Msg_VCreateMsgList(idFmt, args);
   va_end(args);

   result = MsgHint(defaultShow, options, m);

   Msg_FreeMsgList(m);
   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_HintMsgList --
 *
 *      This is same as Msg_Hint except it takes Msg_List as
 *      message arguments.
 *
 * Results:
 *	Returns HINT_CONTINUE, HINT_CANCEL or HINT_NOT_SHOWN.
 *
 * Side effects:
 *      The user may disable future occurrences of the hint
 *	after viewing it.
 *
 *----------------------------------------------------------------------
 */

HintResult
Msg_HintMsgList(Bool defaultShow,	// IN: default enabling
                HintOptions options,	// IN: options
                Msg_List *m)		// IN: Msg_List arguments
{
   HintResult result;

   result = MsgHint(defaultShow, options, m);
   return result;
}


HintResult
MsgHint(Bool defaultShow,	// IN: default enabling
	HintOptions options,	// IN: options
        Msg_List *ml)
{
   MsgState *state = MsgGetState();
   Bool suppress;
   int res = HINT_NOT_SHOWN;
   char *nonLocalizedHint;

   ASSERT(options == HINT_OK || options == HINT_OKCANCEL);

   suppress = Config_GetBool(FALSE, "msg.autoAnswer") ||
              Config_GetBool(FALSE, "msg.noOK");

   nonLocalizedHint = MsgFmt_Asprintf(NULL, ml->format, ml->args, ml->numArgs);
   Log("Msg_Hint: %s (%ssent)\n%s---------------------------------------\n",
       ml->id, suppress ? "not " : "",
       nonLocalizedHint);
   free(nonLocalizedHint);

   if (!suppress) {
      if (state->callback.hint != NULL) {
	 char *formatted = MsgLocalizeList1(ml, NULL);
	 res = state->callback.hint(options, ml->id, formatted);
	 free(formatted);
      }
      if (state->callback.hintList != NULL) {
	 res = state->callback.hintList(options, ml);
      }
   }

   return res;
}


/*
 *----------------------------------------------------------------------
 *
 * MsgHintStdio --
 *
 *   Hint callback for text mode applications
 *
 * Results:
 *   None
 *
 * Side effects:
 *   None.
 *
 *----------------------------------------------------------------------
 */

#define MAX_BUTTONS 4
static HintResult
MsgHintStdio(HintOptions options, // IN: User choice of buttons
             const char *msgID,   // IN: Hint message ID. Unused
             const char *message) // IN: Message to display
{
   HintResult buttons[MAX_BUTTONS];
   int numButtons;
   int choice;
   int res;
   char *hint = Msg_GetString(MSGID(msg.hint) "Hint");
   char *chooseNumber =
      Msg_GetString(MSGID(msg.chooseNumber) "Please choose a number [0-%d]: ");
   char *ok = Msg_GetString(BUTTONID(ok) "OK");
   char *cancel = Msg_GetString(BUTTONID(cancel) "Cancel");

   printf("\n\n"
          "%s %s:\n"
          "%s"
          "\n",
          ProductState_GetName(),
          hint,
          message /* Newline terminated --hpreg */);
   numButtons = 0;

   if ((HINT_OK == options) || (HINT_OKCANCEL == options)) {
      printf("%d) %s\n", numButtons, ok);
      buttons[numButtons] = HINT_CONTINUE;
      numButtons++;
      ASSERT_NOT_IMPLEMENTED(numButtons != MAX_BUTTONS);
   }
   if (HINT_OKCANCEL == options) {
      printf("%d) %s\n", numButtons, cancel);
      buttons[numButtons] = HINT_CANCEL;
      numButtons++;
      ASSERT_NOT_IMPLEMENTED(numButtons != MAX_BUTTONS);
   }
   
   printf("\n");
   for(;;) {
      printf(chooseNumber, numButtons - 1);
      fflush(stdout);

      res = scanf("%d", &choice);
      MsgEatUntilNewLineStdio();
      if (res != 1) {
         continue;
      }
      if ((choice >= 0) && (choice <= numButtons - 1)) {
         break;
      }
   }
   printf("\n");

   free(hint);
   free(chooseNumber);
   free(ok);
   free(cancel);
   return buttons[choice];
}
#undef MAX_BUTTONS


/*
 *----------------------------------------------------------------------
 *
 * Msg_GetString --
 *
 *      Query string from localization database.
 *
 * Results:
 *      A copy of localized string.  The caller must free it.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Msg_GetString(const char *idString)	// IN: string ID and English string
{
   return Util_SafeStrdup(MsgGetString(idString, FALSE, NULL));
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_GetStringSafe --
 *
 *      Query string from localization database, or
 *      if the string isn't in the database just return it.
 *
 * Results:
 *      A copy of localized string.  The caller must free it.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_GetStringSafe(const char *idString)
{
   Err_Number e;

   if (MSG_MAGICAL(idString)) {
      return Msg_GetString(idString);
   }
   if ((e = Err_String2Errno(idString)) != ERR_INVALID) {
      return MsgErrno2LocalString(e, MSGFMT_CURRENT_PLATFORM, idString);
   }
   return Util_SafeStrdup(idString);
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgStripMnemonic --
 *
 *      Removes the mnemonic from a UTF-8 button label.  Button mnemonics
 *      should be indicated with an underscore (e.g. BUTTONID(yes) "_Yes").
 *      Literal underscores can be designated with double underscores ("__").
 *
 * Results:
 *      A copy of the string with the mnemonic removed.  The caller must free
 *      it.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static char *
MsgStripMnemonic(const char *localizedString) // IN: A localized string in UTF-8.
{
   char *s;
   char *p;
   Bool foundMnemonic = FALSE;

   ASSERT(localizedString != NULL);

   s = Util_SafeStrdup(localizedString);
   p = s;
   while ((p = strchr(p, '_')) != NULL) {
      // Remove the underscore.
      memmove(p, p + 1, strlen(p + 1) + 1 /* NUL */);
      if (*p == '_') {
         /*
          * If two underscores occur together, assume that the first is
          * trying to escape the second.
          */
         p++;
      } else {
         // Button labels shouldn't have multiple mnemonics.
         ASSERT(!foundMnemonic);
         foundMnemonic = TRUE;
      }
   }

   return s;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_GetPlainButtonText --
 *
 *      Query for a button label from the localization database and remove the
 *      button's mnemonic.  See MsgStripMnemonic for the mnemonic format.
 *
 * Results:
 *      A copy of the localized string.  The caller must free it.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_GetPlainButtonText(const char *idString) // IN: String ID and English string
{
   return MsgStripMnemonic(MsgGetString(idString, FALSE, NULL));
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_GetLocale --
 *
 *      Get the current language locale for messages.
 *
 * Results:
 *      Locale name string in private storage.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
Msg_GetLocale()
{
   return MsgGetState()->locale;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_SetLocale --
 *
 *      Set the current language locale for messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Message dictionary is loaded.
 *
 *-----------------------------------------------------------------------------
 */

void
Msg_SetLocale(const char *locale,	// IN: the locale
	      const char *binaryName)	// IN: the name of this executable
{
   MsgState *state = MsgGetState();
   char *file;
   Dictionary *dict;

   Log("%s: HostLocale=%s UserLocale=%s\n", __FUNCTION__,
       Unicode_EncodingEnumToName(Unicode_GetCurrentEncoding()),
       locale == NULL ? "NULL" : locale);

   /*
    * Here and below (on Dictionary_Load() failure), we always make sure
    * the dictionary is NULL (not just empty) when we don't have one.
    * MsgGetString() works more efficiently this way.
    * -- edward
    */

   if (locale == NULL) {
       if (state->dict != NULL) {
         Dictionary_Free(state->dict);
         state->dict = NULL;
       }
       free(state->locale);
       state->locale = NULL;
       return;
   }

   /*
    * Load dictionary.
    *
    * Make sure we keep the old dictionary (if any) until we're
    * successful, for two reasons: Dictionary calls Msg functions,
    * don't lose the old locale if it's valid but the new one isn't.
    */

   file = Msg_GetMessageFilePath(locale, binaryName, "vmsg");
   dict = Dictionary_Create();
   ASSERT(dict != NULL);
   if (!Dictionary_LoadWithDefaultEncoding(dict, file, DICT_NOT_DEFAULT,
					   STRING_ENCODING_UTF8)) {
      Msg_Reset(TRUE);
      Warning("Cannot load message dictionary \"%s\".\n", file);
      Dictionary_Free(dict);
   } else {
      if (state->dict != NULL) {
	 Dictionary_Free(state->dict);
      }
      state->dict = dict;
      free(state->locale);
      state->locale = strdup(locale);
      ASSERT_MEM_ALLOC(state->locale != NULL);
   }
   free(file);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_GetMessageFilePath --
 *
 *      Compute the locale-specific message file path.
 *
 * Results:
 *      Message file in an allocated string.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_GetMessageFilePath(const char *locale,	// IN: the locale
		       const char *binaryName,	// IN: name of this executable
		       const char *extension)	// IN: file extension
{
   char *libdir;
   char *path;

   /*
    * Find the message dictionary, based on locale and our program
    * name (binaryName).
    */

#ifdef _WIN32
   libdir = W32Util_GetInstalledFilePath(NULL);
#else
   libdir = LocalConfig_GetPathName(DEFAULT_LIBDIRECTORY, CONFIG_VMWAREDIR);
#endif
   ASSERT_NOT_IMPLEMENTED(libdir != NULL);
   path = Str_Asprintf(NULL, "%s%smessages%s%s%s%s.%s",
                       libdir, DIRSEPS, DIRSEPS, locale, DIRSEPS,
                       binaryName, extension);
   ASSERT_MEM_ALLOC(path != NULL);
   free(libdir);

   return path;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_FormatFloat --
 *
 *    Format 'value' to a string, according to the currently set locale --hpreg
 *
 * Results:
 *    The allocated string
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_FormatFloat(double value,           // IN
                unsigned int precision) // IN
{
#if !_WIN32
   char *fmt;
   char *out;

   fmt = Str_Asprintf(NULL, "%%'.%uf", precision);
   ASSERT_MEM_ALLOC(fmt);
   /*
    * XXX We do not switch to the locale currently set yet. We will do it
    *     as soon as Edward's code to produce system error strings in the
    *     current locale is checked in --hpreg
    */
   out = Str_Asprintf(NULL, fmt, value);
   ASSERT_MEM_ALLOC(out);
   free(fmt);

   return out;
#else
   /*
    * Poorly documented:
    * . The ' construct doesn't work in (s)printf()
    * . setlocale() affects (s)printf(), SetThreadLocale() doesn't
    *
    * On top of that, specifying the precision without changing any other
    * locale number formatting parameter in GetNumberFormat() is a royal pain
    * in the ass. So I give up with number localization for now :( --hpreg
    */

   char *fmt;
   char buf[64];
   int result;
   char *out;

   fmt = Str_Asprintf(NULL, "%%.%uf", precision);
   ASSERT_MEM_ALLOC(fmt);
   result = Str_Snprintf(buf, sizeof(buf), fmt, value);
   ASSERT_NOT_IMPLEMENTED(result != -1);
   free(fmt);
   out = strdup(buf);
   ASSERT_MEM_ALLOC(out);

   return out;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * Msg_FormatSizeInBytes --
 *
 *      Format a size (in bytes) to a string in a user-friendly way.
 *
 *      Example: 160041885696 -> "149.1 GB"
 *
 * Results:
 *      The allocated, NUL-terminated string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
Msg_FormatSizeInBytes(uint64 size) // IN
{
   char const *fmt;
   double sizeInSelectedUnit;
   unsigned int precision;
   char *sizeString;
   char *result;
   static const double epsilon = 0.01;
   double delta;

   if (size >= CONST64U(1) << 40 /* 1 TB */) {
      fmt = MSGID(msg.terabyte.abbreviation) "%s TB";
      sizeInSelectedUnit = (double)size / (CONST64U(1) << 40);
      precision = 1;
   } else if (size >= CONST64U(1) << 30 /* 1 GB */) {
      fmt = MSGID(msg.gigabyte.abbreviation) "%s GB";
      sizeInSelectedUnit = (double)size / (CONST64U(1) << 30);
      precision = 1;
   } else if (size >= CONST64U(1) << 20 /* 1 MB */) {
      fmt = MSGID(msg.megabyte.abbreviation) "%s MB";
      sizeInSelectedUnit = (double)size / (CONST64U(1) << 20);
      precision = 1;
   } else if (size >= CONST64U(1) << 10 /* 1 KB */) {
      fmt = MSGID(msg.kilobyte.abbreviation) "%s KB";
      sizeInSelectedUnit = (double)size / (CONST64U(1) << 10);
      precision = 1;
   } else if (size >= CONST64U(2) /* 2 bytes */) {
      fmt = MSGID(msg.byte.twoOrMore) "%s bytes";
      sizeInSelectedUnit = (double)size;
      precision = 0; // No fractional byte.
   } else if (size >= CONST64U(1) /* 1 byte */) {
      fmt = MSGID(msg.byte.one) "%s byte";
      sizeInSelectedUnit = (double)size;
      precision = 0; // No fractional byte.
   } else {
      ASSERT(size == CONST64U(0) /* 0 bytes */);
      fmt = MSGID(msg.byte.zero) "%s bytes";
      sizeInSelectedUnit = (double)size;
      precision = 0; // No fractional byte.
   }

   /*
    * We cast to uint32 instead of uint64 here because of a problem with the
    * NetWare Tools build. However, it's safe to cast to uint32 since we have
    * already reduced the range of sizeInSelectedUnit above.
    */
   // If it would display with .0, round it off and display the integer value.
   delta = (uint32)(sizeInSelectedUnit + 0.5) - sizeInSelectedUnit;
   if (delta < 0) {
      delta = -delta;
   }
   if (delta <= epsilon) {
      precision = 0;
      sizeInSelectedUnit = (double)(uint32)(sizeInSelectedUnit + 0.5);
   }

   sizeString = Msg_FormatFloat(sizeInSelectedUnit, precision);
   result = Msg_Format(fmt, sizeString);
   free(sizeString);

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgGetString --
 *
 *      Extract and localize a message string from the supplied
 *	ID-string pair.
 *
 * Results:
 *	Localized message.
 *      ID in supplied buffer id (if id != NULL).
 *
 * Side effects:
 *	Returned message might share storage with idString,
 *	so don't free idString prematurely.
 *
 *-----------------------------------------------------------------------------
 */

const char *
MsgGetString(const char *idString,  // IN: message ID and English message
	     Bool noLocalize,       // IN: don't localize
	     char *id)              // OUT: message ID.  Optional.
{
   const char *idp, *strp;
   Dictionary *dict = noLocalize ? NULL : MsgGetState()->dict;
   char idBuf[MSG_MAX_ID];

   /*
    * All message strings must be prefixed by the message ID.
    */

   ASSERT(idString != NULL);
   ASSERT(MSG_MAGICAL(idString));

   /*
    * Find the beginning of the ID (idp) and the string (strp).
    */

   idp = idString + MSG_MAGIC_LEN;
   ASSERT(*idp == '(');
   idp++;
   strp = strchr(idp, ')');
   ASSERT(strp != NULL);
   strp++;

   /*
    * Copy ID out into a null-terminated string.
    * This is either the out parameter or a local buffer if we are going
    * to look up the localized string.
    *
    * Don't mess with the condition.  -- edward
    */

   if (id != NULL || (dict != NULL && (id = idBuf))) {
      size_t len = strp - idp - 1;
      ASSERT_NOT_IMPLEMENTED(len <= MSG_MAX_ID - 1);
      memcpy(id, idp, len);
      id[len] = '\0';
   }

   /*
    * Look up the localized string.
    */

   if (dict != NULL) {
      const char *loc = MsgLookUpString(dict, id);
      if (loc != NULL) {
	 return loc;
      }
   }
   return strp;
}


/*
 *-----------------------------------------------------------------------------
 *
 * MsgLookUpString --
 *
 *	Look up a localized string by ID.
 *
 * Results:
 *	Localized string or NULL.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static const char *
MsgLookUpString(Dictionary *dict,	// IN: localization dictionary
                const char *id)		// IN: string ID
{
   const char *defaultValue = NULL;

   /*
    * Don't use the English string as the Dictionary_Get() default,
    * since it'll be copied and stored.  The only thing we lose
    * is conflict detection, which we can do at compile time anyway.
    * -- edward
    */

   return *(const char **)Dictionary_Get(dict, &defaultValue, DICT_STRING, id);
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_SetCallback --
 *
 *      renews all the msg callbacks
 *
 * Results:
 *      void
 *
 * Side effects:
 *      future calls that require GUI interaction will
 *      call the update callback
 *
 *----------------------------------------------------------------------
 */

void
Msg_SetCallback(MsgCallback *cb) // IN
{
   MsgState *state = MsgGetState();
   ASSERT(cb->post);
   /*
    * either question or questionList call back function
    * should suffice to handle posting a question.
    */
   ASSERT(cb->question || cb->questionList);
   ASSERT(cb->progress);
   ASSERT(cb->hint);

   state->callback = *cb;
}


void
Msg_GetCallback(MsgCallback *cb) // OUT
{
   MsgState *state = MsgGetState();
   *cb = state->callback;
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_Exit --
 *
 *      Clean up any memory allocated.
 *
 * Results:
 *      void
 *      
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void 
Msg_Exit()
{
   MsgState *state = MsgGetState();

   Msg_Reset(FALSE);
   if (state->dict != NULL) {
      Dictionary_Free(state->dict);
   }
   free(state->locale);
   free(state);
   msgState = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Msg_LoadMessageFile --
 *
 *      Load the specified file in the message dictionary.
 *
 * Results:
 *      TRUE if the specified file is loaded in the dictionary.
 *
 * Side effects:
 *      Existing message dictionary is modified.
 *      New dictionary is create if not already exist
 *
 *----------------------------------------------------------------------
 */

Bool
Msg_LoadMessageFile(const char *locale,   // IN
                    const char *fileName) // IN
{
   MsgState *state = MsgGetState();

   if (!fileName) {
      return FALSE;
   }

   if (state->dict != NULL) {
      /*
       * XXX -- Todo: need to reset the dictionary if the locale do not match
       */

      if (!Dictionary_Append(state->dict, fileName, DICT_NOT_DEFAULT)) {
	 Msg_Reset(TRUE);
	 Warning("Cannot load message dictionary \"%s\".\n", fileName);
	 return FALSE;
      }

   } else {
      /*
       * Create a new dictionary if it does not exist already
       */

      Dictionary *dict = Dictionary_Create();
      ASSERT(dict != NULL);

      if (!Dictionary_LoadWithDefaultEncoding(dict, fileName, DICT_NOT_DEFAULT,
					      STRING_ENCODING_UTF8)) {
	 Msg_Reset(TRUE);
	 Warning("Cannot load message dictionary \"%s\".\n", fileName);
	 Dictionary_Free(dict);
	 return FALSE;
      }

      state->dict = dict;

      free(state->locale);
      state->locale = strdup(locale);
   }

   return TRUE;
}


Msg_List *
Msg_GetMsgList(void)
{
   MsgState *state = MsgGetState();

   return state->head;
}

Msg_List *
Msg_GetMsgListAndReset(void)
{
   MsgState *state = MsgGetState();
   Msg_List *list = state->head;

   state->head = NULL;
   state->tailp = &state->head;

   return list;
}

void
Msg_FreeMsgList(Msg_List *messages)
{
   Msg_List *m;
   Msg_List *next;

   for (m = messages; m != NULL; m = next) {
      free(m->format);
      free(m->id);
      MsgFmt_FreeArgs(m->args, m->numArgs);
      next = m->next;
      free(m);
   }
}

static void
MsgLogList(const char *who,
           const char *label,
	   Msg_List *messages)
{
   Msg_List *m;

   Log("%s:%s%s\n", who, *label ? " " : "", label);
   for (m = messages; m != NULL; m = m->next) {
      char *formatted = MsgFmt_Asprintf(NULL, m->format, m->args, m->numArgs);
      Log("[%s] %s", m->id, formatted);
      free(formatted);
   }
   Log("----------------------------------------\n");
}


char *
Msg_LocalizeList(Msg_List *messages)   // IN
{
   DynBuf dynBuf;

   DynBuf_Init(&dynBuf);
   MsgLocalizeList(messages, &dynBuf);
   return((char *) DynBuf_Detach(&dynBuf));
}


static void
MsgLocalizeList(Msg_List *messages,
                DynBuf *b)
{
   Msg_List *m;

#ifdef XXX_MSGFMT_DEBUG
   Warning("XXX localizing:\n");
   for (m = messages; m != NULL; m = m->next) {
      MsgPrintMsgFmt(m->format, m->args, m->numArgs);
   }
   Warning("XXX ----------------------------------------\n");
#endif

   for (m = messages; m != NULL; m = m->next) {
      size_t len;
      char *formatted = MsgLocalizeList1(m, &len);
      Bool status = DynBuf_Append(b, formatted, len);
      ASSERT_MEM_ALLOC(status);
      free(formatted);
   }
   DynBuf_Append(b, "", 1);
}

static char *
MsgLocalizeList1(Msg_List *m,
		 size_t *len)
{
   MsgState *state = MsgGetState();
   const char *fmtLocalized;
   int i;
   char *formatted;

   if (state->dict == NULL ||
       (fmtLocalized = MsgLookUpString(state->dict, m->id)) == NULL) {
      fmtLocalized = m->format;
   }

   /*
    * Localize arguments that need it.
    */

   for (i = 0; i < m->numArgs; i++) {
      MsgFmt_Arg *a = m->args + i;

      if (a->type == MSGFMT_ARG_STRING8) {
	 if (MSG_MAGICAL(a->v.string8)) {
	    a->p.localString =
	       (char *) MsgGetString(a->v.string8, FALSE, NULL);
	 } else {
	    a->p.localString = NULL;
	 }
      } else if (a->type == MSGFMT_ARG_ERRNO) {
	 a->p.localString =
	    MsgErrno2LocalString(a->e.number, a->e.platform, a->v.string8);
      }
   }

   formatted = MsgFmt_Asprintf(len, fmtLocalized, m->args, m->numArgs);
   ASSERT_MEM_ALLOC(formatted);

   /*
    * Clean up localized arguments.
    */

   for (i = 0; i < m->numArgs; i++) {
      MsgFmt_Arg *a = m->args + i;

      if (a->type == MSGFMT_ARG_ERRNO) {
	 free(a->p.localString);
	 a->p.localString = NULL;
      } else if (a->type == MSGFMT_ARG_STRING8) {
	 a->p.localString = NULL;
      }
   }

   return formatted;
}


/*
 *----------------------------------------------------------------------
 *
 * MsgErrno2LocalString --
 *
 *      Localize an error number.
 *
 * Results:
 *      The localized error string in allocated storage.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
MsgErrno2LocalString(Err_Number errorNumber,      // IN
		     MsgFmt_ArgPlatform platform, // IN
		     const char *englishString)   // IN
{
   MsgState *state = MsgGetState();
   char *s;

   ASSERT(platform != MSGFMT_PLATFORM_UNKNOWN);
   ASSERT_NOT_IMPLEMENTED(platform == MSGFMT_CURRENT_PLATFORM);

   if ((s = Err_Errno2LocalString(errorNumber)) != NULL) {
      return s;
   }
   if (state->locale == NULL || strcmp(state->locale, "en") == 0) {
      return Util_SafeStrdup(englishString);
   }
   return platform == MSGFMT_PLATFORM_WINDOWS ?
          Msg_Format(MSGID(msg.systemErrorWindows)
		     "Error %d (0x%x) [%s]",
		     errorNumber, errorNumber, englishString) :
          Msg_Format(MSGID(msg.systemError) "Error %d [%s]",
		     errorNumber, englishString);
}

#ifdef XXX_MSGFMT_DEBUG // {
static void
MsgPrintMsgFmt(const char *format,
               MsgFmt_Arg *args,
               int numArgs)
{
   int i;

   Warning("XXX format \"%s\" %d arg(s).\n", format, numArgs);
   for (i = 0; i < numArgs; i++) {
      switch (args[i].type) {
      case MSGFMT_ARG_INT32:
	 Warning("    XXX arg %d int32 0x%08x %d\n",
		 i, args[i].v.unsigned32, args[i].v.signed32);
	 break;
      case MSGFMT_ARG_INT64:
	 Warning("    XXX arg %d int64 0x%016"FMT64"x %"FMT64"d\n",
		 i, args[i].v.unsigned64, args[i].v.signed64);
	 break;
      case MSGFMT_ARG_PTR32:
	 Warning("    XXX arg %d ptr32 0x%08x\n",
		 i, args[i].v.unsigned32);
	 break;
      case MSGFMT_ARG_PTR64:
	 Warning("    XXX arg %d ptr32 0x%016"FMT64"x\n",
		 i, args[i].v.unsigned64);
	 break;
      case MSGFMT_ARG_FLOAT64:
	 Warning("    XXX arg %d float64 %.30g\n", i, args[i].v.float64);
	 break;
      case MSGFMT_ARG_STRING8:
	 Warning("    XXX arg %d string8 \"%s\"\n", i, args[i].v.string8);
	 break;
      case MSGFMT_ARG_STRING16:
      case MSGFMT_ARG_STRING32:
      default:
	 Warning("    XXX arg %d type %d\n", i, args[i].type);
      }
   }
}
#endif // }


/*
 *----------------------------------------------------------------------
 *
 * Msg_AppendMsgList --
 *
 *      Append the message (id+fmt+args) to MsgState. The args parameter
 *      will be freed when MsgPost is called later. The caller should not
 *      free the args.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Allocates memory for new Msg_List. Modifies MsgState.
 *
 *----------------------------------------------------------------------
 */

void
Msg_AppendMsgList(char* id,
                  char* fmt,
                  MsgFmt_Arg* args,
                  int numArgs)
{
   Msg_List* m;
   MsgState *state = MsgGetState();
   
   m = Util_SafeMalloc(sizeof(Msg_List));

   m->id = Util_SafeStrdup(id);
   m->format = Util_SafeStrdup(fmt);
   m->numArgs = numArgs;
   m->args = args;
   m->next = NULL;

   *state->tailp = m;
   state->tailp = &m->next;
}


/* $Id$ */
/* Test suite for ARTparse. */

#include "config.h"
#include "clibrary.h"

#include "inn/buffer.h"
#include "inn/innconf.h"
#include "inn/messages.h"
#include "libinn.h"
#include "libtest.h"

#include "../../innd/innd.h"

/* A table of paths to articles and corresponding error messages. */
const struct {
    const char *path;
    const char *error;
} articles[] = {
    { "../storage/articles/1",         "" },
    { "../storage/articles/2",         "" },
    { "../storage/articles/3",         "" },
    { "../storage/articles/4",         "" },
    { "../storage/articles/5",         "" },
    { "../storage/articles/bad-msgid", "" },
    { "../storage/articles/bad-subj",  "" },
    { "../storage/articles/6",
      "437 Article of 8193 bytes exceeds local limit of 8192 bytes" },
    { "../storage/articles/bad-empty",
      "437 Empty article" },
    { "../storage/articles/bad-hdr-empty",
      "437 Body of header is all blanks in \"From\" header" },
    { "../storage/articles/bad-hdr-nospc",
      "437 No colon-space in \"Test:<-he: re\" header" },
    { "../storage/articles/bad-hdr-space",
      "437 Space before colon in \"Test\" header" },
    { "../storage/articles/bad-hdr-trunc",
      "437 No colon-space in \"Test:\" header" },
    { "../storage/articles/bad-long-cont",
      "437 Header line too long (1025 bytes)" },
    { "../storage/articles/bad-long-hdr",
      "437 Header line too long (1025 bytes)" },
    { "../storage/articles/bad-no-body",
      "437 No body" },
    { "../storage/articles/bad-no-header",
      "437 No headers" },
    { "../storage/articles/bad-nul-body",
      "437 Nul character in body" },
    { "../storage/articles/bad-nul-header",
      "437 Nul character in header" }
};

/* Create enough of an innconf struct to be able to run ARTparse.  Set
   logipaddr to false so that we don't have to initialize enough in the
   channel to get RChostname working. */
static void
fake_innconf(void)
{
    if (innconf != NULL) {
        free(innconf->pathetc);
        free(innconf);
    }
    innconf = xmalloc(sizeof(*innconf));
    innconf->logipaddr = false;
    innconf->maxartsize = 8 * 1024;
    innconf->pathetc = xstrdup("../storage/etc");
}

/* Create a fake channel with just enough data filled in to be able to use it
   to test article parsing. */
static CHANNEL *
fake_channel(void)
{
    CHANNEL *cp;
    static const CHANNEL CHANnull;

    cp = xmalloc(sizeof(CHANNEL));
    *cp = CHANnull;
    cp->Type = CTnntp;
    cp->State = CSgetheader;
    return cp;
}

/* Initialize things enough to be able to call ARTparse and friends.  This
   only has to be called once. */
static void
initialize(void)
{
    if (access("../storage/etc/overview.fmt", F_OK) < 0)
        if (access("storage/etc/overview.fmt", F_OK) == 0)
            if (chdir("innd") != 0)
                sysdie("Cannot cd to innd");
    fake_innconf();
    Log = fopen("/dev/null", "w");
    if (Log == NULL)
        sysdie("Cannot open /dev/null");
    fdreserve(4);
    buffer_set(&Path, "", 1);
    ARTsetup();
}

/* Given the test number, a path to an article and an expected error message
   (which may be ""), create a channel, run the article through ARTparse
   either all at once or, if slow is true, one character at a time, and check
   the result.  If shift is true, shift the start of the article in the buffer
   by a random amount.  Produces three test results. */
static void
ok_article(int n, const char *path, const char *error, bool slow, bool shift)
{
    CHANNEL *cp;
    char *article, *wire;
    size_t i, len, wirelen, offset;
    struct stat st;
    bool okay = true;
    CHANNELSTATE expected;

    article = ReadInFile(path, &st);
    len = st.st_size;
    wire = ToWireFmt(article, len, &wirelen);
    cp = fake_channel();
    offset = shift ? random() % 50 : 0;
    cp->Start = offset;
    cp->Next = offset;
    buffer_resize(&cp->In, wirelen + offset);
    memset(cp->In.data, '\0', offset);
    cp->In.used = offset;
    ARTprepare(cp);
    if (slow) {
        for (i = 0; i < wirelen; i++) {
            cp->In.data[i + offset] = wire[i];
            cp->In.used++;
            message_handlers_warn(0);
            ARTparse(cp);
            message_handlers_warn(1, message_log_stderr);
            if (i < wirelen - 1 && cp->State != CSeatarticle)
                if (cp->State != CSgetheader && cp->State != CSgetbody) {
                    okay = false;
                    warn("Bad state %d at %ld", cp->State, (long) i);
                    break;
                }
        }
    } else {
        buffer_append(&cp->In, wire, wirelen);
        cp->In.used = offset + wirelen;
        message_handlers_warn(0);
        ARTparse(cp);
        message_handlers_warn(1, message_log_stderr);
    }
    if (wirelen > (size_t) innconf->maxartsize)
        expected = CSgotlargearticle;
    else if (wirelen == 5)
        expected = CSnoarticle;
    else
        expected = CSgotarticle;
    ok_int(n++, expected, cp->State);
    ok_int(n++, wirelen, cp->Next - cp->Start);
    ok_string(n++, error, cp->Error);
    free(article);
    free(wire);
    free(cp);
}

int
main(void)
{
    size_t i;
    int n = 1;

    test_init(ARRAY_SIZE(articles) * 3 * 4);
    initialize();
    message_handlers_notice(0);

    for (i = 0; i < ARRAY_SIZE(articles); i++) {
        ok_article(n, articles[i].path, articles[i].error, false, false);
        n += 3;
        ok_article(n, articles[i].path, articles[i].error, true, false);
        n += 3;
        ok_article(n, articles[i].path, articles[i].error, false, true);
        n += 3;
        ok_article(n, articles[i].path, articles[i].error, true, true);
        n += 3;
    }
    return 0;
}

/*
 * checklist.c - Jakesoft Checklists
 *
 * Reusable checklists for Palm OS 3.0 and newer.
 *   - Templates: saved lists of items you reuse (e.g. a packing list).
 *   - Lists: a copy of a template you check off as you go.
 *   - Beam templates or lists in progress to another Palm over IR.
 *
 * Everything is stored in one database (type 'Data', creator 'JsCL').
 * Each record is one template or list:
 *
 *     UInt8  kind        0 = list in progress, 1 = template
 *     UInt8  count       number of items
 *     char   name[]      NUL-terminated
 *     count x { UInt8 done; char text[]; }   text is NUL-terminated
 *
 * The same bytes are what gets beamed.
 *
 * Plain C89, PalmOS.h from SDK 3.5.
 */

#include <PalmOS.h>
#include "checklist_res.h"

#define CREATOR         'JsCL'
#define DB_TYPE         'Data'
#define DB_NAME         "JakesoftChecklists"

#define KIND_LIST       0
#define KIND_TEMPLATE   1

#define MAX_ITEMS       100
#define ITEM_LEN        40          /* includes the terminating NUL */
#define NAME_LEN        40
#define MAX_RECS        200
#define MAX_REC_BYTES   (2 + NAME_LEN + MAX_ITEMS * (1 + ITEM_LEN))
#define NEW_REC         0xFFFF      /* same value as dmMaxRecordIndex */

#define MAIN_VIS        9
#define CHECK_VIS       11
#define EDIT_VIS        11
#define PICK_VIS        6

#define ROM_3_0         0x03000000L

typedef struct {
    UInt8 kind;
    UInt8 count;
    Char  name[NAME_LEN];
    Char  text[MAX_ITEMS][ITEM_LEN];
    UInt8 done[MAX_ITEMS];
} ListData;

typedef struct {
    UInt16 listID;
    UInt16 sclID;
    Int16  top;
    Int16  total;
    Int16  vis;
} ScrollState;

/* ------------------------------------------------------------------ */
/* Globals (only touched when the app is running normally)             */
/* ------------------------------------------------------------------ */

static DmOpenRef   g_db = 0;
static ListData   *g_list = 0;          /* the list/template being viewed */
static UInt16      g_rec = 0;           /* its record index */
static UInt16      g_return = frmMain;  /* where the editor's Done goes */
static UInt8       g_view = KIND_LIST;  /* main form tab */
static UInt16      g_map[MAX_RECS];     /* main list row -> record index */
static UInt16      g_mapCount = 0;
static UInt16      g_pickMap[MAX_RECS]; /* template picker row -> record */
static UInt16      g_pickCount = 0;
static ScrollState g_scr;               /* scroll state of the current form */
static Char        g_textBuf[ITEM_LEN]; /* result of AskText() */

#define OBJ(frm, id)  FrmGetObjectPtr((frm), FrmGetObjectIndex((frm), (id)))

/* ------------------------------------------------------------------ */
/* Record format helpers (no globals, so beam-receive can use them)    */
/* ------------------------------------------------------------------ */

static Boolean SkipStr(const UInt8 *p, UInt32 size, UInt32 *pos, UInt16 maxLen)
{
    UInt32 start = *pos;

    while (*pos < size && p[*pos])
        (*pos)++;
    if (*pos >= size)
        return false;                   /* no terminator */
    if (*pos - start >= maxLen)
        return false;                   /* too long */
    (*pos)++;
    return true;
}

static Boolean ValidRecord(const UInt8 *p, UInt32 size)
{
    UInt32 pos;
    UInt16 i, n;

    if (size < 3 || size > MAX_REC_BYTES)
        return false;
    if (p[0] > KIND_TEMPLATE || p[1] > MAX_ITEMS)
        return false;
    n = p[1];
    pos = 2;
    if (!SkipStr(p, size, &pos, NAME_LEN))
        return false;
    for (i = 0; i < n; i++) {
        if (pos >= size)
            return false;
        pos++;                          /* done flag */
        if (!SkipStr(p, size, &pos, ITEM_LEN))
            return false;
    }
    return pos == size;
}

static void CopyStr(Char *dst, const UInt8 *src, UInt16 maxLen)
{
    StrNCopy(dst, (const Char *)src, maxLen - 1);
    dst[maxLen - 1] = 0;
}

/* p must already have passed ValidRecord() */
static void ParseRecord(const UInt8 *p, ListData *d)
{
    UInt32 pos = 2;
    UInt16 i;

    d->kind = p[0];
    d->count = p[1];
    CopyStr(d->name, p + pos, NAME_LEN);
    pos += StrLen((const Char *)(p + pos)) + 1;
    for (i = 0; i < d->count; i++) {
        d->done[i] = p[pos] ? 1 : 0;
        pos++;
        CopyStr(d->text[i], p + pos, ITEM_LEN);
        pos += StrLen((const Char *)(p + pos)) + 1;
    }
}

static UInt32 BuildRecord(const ListData *d, UInt8 *out)
{
    UInt32 pos = 2;
    UInt16 i, len;

    out[0] = d->kind;
    out[1] = d->count;
    len = StrLen(d->name) + 1;
    MemMove(out + pos, d->name, len);
    pos += len;
    for (i = 0; i < d->count; i++) {
        out[pos++] = d->done[i];
        len = StrLen(d->text[i]) + 1;
        MemMove(out + pos, d->text[i], len);
        pos += len;
    }
    return pos;
}

/* ------------------------------------------------------------------ */
/* Database                                                            */
/* ------------------------------------------------------------------ */

static Boolean SaveList(UInt16 *rec)
{
    UInt8    *buf;
    UInt32    size;
    MemHandle h = 0, nh;
    MemPtr    dst;
    Boolean   ok = false;

    buf = (UInt8 *)MemPtrNew(MAX_REC_BYTES);
    if (!buf)
        return false;
    size = BuildRecord(g_list, buf);

    if (*rec == NEW_REC) {
        h = DmNewRecord(g_db, rec, size);
    } else {
        h = DmGetRecord(g_db, *rec);
        if (h) {
            nh = DmResizeRecord(g_db, *rec, size);
            if (nh)
                h = nh;
            else {
                DmReleaseRecord(g_db, *rec, false);
                h = 0;
            }
        }
    }
    if (h) {
        dst = MemHandleLock(h);
        DmWrite(dst, 0, buf, size);
        MemHandleUnlock(h);
        DmReleaseRecord(g_db, *rec, true);
        ok = true;
    }
    MemPtrFree(buf);
    return ok;
}

static void SaveCurrent(void)
{
    if (!SaveList(&g_rec))
        FrmAlert(alertSaveFail);
}

static Boolean LoadList(UInt16 rec)
{
    MemHandle    h = DmQueryRecord(g_db, rec);
    const UInt8 *p;
    Boolean      ok = false;

    if (!h)
        return false;
    p = (const UInt8 *)MemHandleLock(h);
    if (ValidRecord(p, MemHandleSize(h))) {
        ParseRecord(p, g_list);
        g_rec = rec;
        ok = true;
    }
    MemHandleUnlock(h);
    return ok;
}

/* Name and progress of a stored record, for drawing rows. */
static void RecInfo(UInt16 rec, Char *name, UInt16 *count, UInt16 *done)
{
    MemHandle    h = DmQueryRecord(g_db, rec);
    const UInt8 *p;
    UInt32       pos, size;
    UInt16       i;

    name[0] = 0;
    *count = 0;
    *done = 0;
    if (!h)
        return;
    size = MemHandleSize(h);
    p = (const UInt8 *)MemHandleLock(h);
    if (ValidRecord(p, size)) {
        CopyStr(name, p + 2, NAME_LEN);
        *count = p[1];
        pos = 2 + StrLen((const Char *)(p + 2)) + 1;
        for (i = 0; i < *count; i++) {
            if (p[pos])
                (*done)++;
            pos++;
            pos += StrLen((const Char *)(p + pos)) + 1;
        }
    } else {
        StrCopy(name, "(damaged)");
    }
    MemHandleUnlock(h);
}

/* Fills map with the records of one kind, newest first. */
static UInt16 BuildMap(UInt8 kind, UInt16 *map)
{
    Int32        i;
    UInt16       c = 0;
    MemHandle    h;
    const UInt8 *p;

    for (i = (Int32)DmNumRecords(g_db) - 1; i >= 0 && c < MAX_RECS; i--) {
        h = DmQueryRecord(g_db, (UInt16)i);
        if (!h)
            continue;
        p = (const UInt8 *)MemHandleLock(h);
        if (MemHandleSize(h) >= 2 && p[0] == kind)
            map[c++] = (UInt16)i;
        MemHandleUnlock(h);
    }
    return c;
}

static void AddStarterTemplate(void)
{
    static const Char *items[] = {
        "Passport / photo ID", "Wallet and cards", "Phone and charger",
        "Medications", "Toothbrush and toothpaste", "Underwear",
        "Socks", "Shirts", "Pants / shorts", "Jacket",
        "Comfortable shoes", "Glasses / sunglasses",
        "Handheld and cables", "Tickets / itinerary"
    };
    UInt16 i, rec = NEW_REC;

    MemSet(g_list, sizeof(ListData), 0);
    g_list->kind = KIND_TEMPLATE;
    StrCopy(g_list->name, "Trip packing (sample)");
    for (i = 0; i < sizeof(items) / sizeof(items[0]); i++)
        StrCopy(g_list->text[i], items[i]);
    g_list->count = (UInt8)i;
    SaveList(&rec);
}

/* ------------------------------------------------------------------ */
/* Beaming (Exchange Manager)                                          */
/* ------------------------------------------------------------------ */

/* Written without string literals so it is safe to call from launch
 * codes that arrive without application globals. */
static void RegisterBeaming(void)
{
    Char   ext[4];
    UInt32 romVer = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVer);
    if (romVer < ROM_3_0)
        return;
    ext[0] = 'j'; ext[1] = 'c'; ext[2] = 'l'; ext[3] = 0;
    ExgRegisterData(CREATOR, exgRegExtensionID, ext);
}

static void BeamRecord(UInt16 rec)
{
    ExgSocketType s;
    Err           err;
    MemHandle     h;
    UInt8        *p;
    UInt32        size, off, n;
    UInt32        romVer = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVer);
    if (romVer < ROM_3_0) {
        FrmAlert(alertNoBeam);
        return;
    }
    h = DmQueryRecord(g_db, rec);
    if (!h)
        return;
    size = MemHandleSize(h);
    p = (UInt8 *)MemHandleLock(h);

    MemSet(&s, sizeof(s), 0);
    s.description = (Char *)(p + 2);        /* list name */
    s.name = "checklist.jcl";
    s.target = CREATOR;
    s.length = size;

    err = ExgPut(&s);
    if (err == errNone) {
        off = 0;
        while (err == errNone && off < size) {
            n = ExgSend(&s, p + off, size - off, &err);
            if (n == 0 && err == errNone)
                err = exgErrBadData;
            off += n;
        }
        err = ExgDisconnect(&s, err);
    }
    MemHandleUnlock(h);

    if (err != errNone && err != exgErrUserCancel)
        FrmAlert(alertBeamFail);
}

/*
 * Receives a beamed record and adds it to the database.  Uses no globals:
 * pass db = 0 and it opens the database itself (app not running), or pass
 * the running app's open database.  Returns the kind of the received
 * record in *kindOut.
 */
static Err ReceiveBeam(ExgSocketPtr s, DmOpenRef db, UInt8 *kindOut)
{
    Err       err;
    UInt8    *buf;
    UInt32    total = 0, n;
    Boolean   own = false;
    MemHandle h;
    MemPtr    dst;
    UInt16    idx;

    *kindOut = 0xFF;
    err = ExgAccept(s);
    if (err != errNone)
        return err;

    buf = (UInt8 *)MemPtrNew(MAX_REC_BYTES + 1);
    if (!buf) {
        ExgDisconnect(s, exgMemError);
        return exgMemError;
    }

    while (err == errNone) {
        n = ExgReceive(s, buf + total, MAX_REC_BYTES + 1 - total, &err);
        if (err != errNone || n == 0)
            break;
        total += n;
        if (total > MAX_REC_BYTES)
            err = exgErrBadData;
    }

    if (err == errNone && !ValidRecord(buf, total))
        err = exgErrBadData;

    if (err == errNone) {
        if (!db) {
            db = DmOpenDatabaseByTypeCreator(DB_TYPE, CREATOR, dmModeReadWrite);
            own = true;
        }
        if (!db) {
            err = exgErrAppError;       /* run the app once to create it */
        } else {
            idx = dmMaxRecordIndex;
            h = DmNewRecord(db, &idx, total);
            if (h) {
                dst = MemHandleLock(h);
                DmWrite(dst, 0, buf, total);
                MemHandleUnlock(h);
                DmReleaseRecord(db, idx, true);
                *kindOut = buf[0];
            } else {
                err = exgMemError;
            }
            if (own)
                DmCloseDatabase(db);
        }
    }

    MemPtrFree(buf);
    ExgDisconnect(s, err);
    return err;
}

/* ------------------------------------------------------------------ */
/* Scrolling helpers                                                   */
/* ------------------------------------------------------------------ */

static void ScrollSync(FormPtr frm, ScrollState *s, Boolean redraw)
{
    Int16        max = (s->total > s->vis) ? (s->total - s->vis) : 0;
    ListPtr      lst = (ListPtr)OBJ(frm, s->listID);
    ScrollBarPtr sb = (ScrollBarPtr)OBJ(frm, s->sclID);

    if (s->top > max) s->top = max;
    if (s->top < 0)   s->top = 0;
    LstSetTopItem(lst, s->top);
    SclSetScrollBar(sb, s->top, 0, max, s->vis - 1);
    if (redraw)
        LstDrawList(lst);
}

static void SetupList(FormPtr frm, ScrollState *s, UInt16 listID, UInt16 sclID,
                      Int16 total, Int16 vis, Int16 top,
                      ListDrawDataFuncPtr fn, Boolean redraw)
{
    ListPtr lst = (ListPtr)OBJ(frm, listID);

    LstSetListChoices(lst, NULL, total);
    LstSetDrawFunction(lst, fn);
    LstSetSelection(lst, noListSelection);
    s->listID = listID;
    s->sclID = sclID;
    s->total = total;
    s->vis = vis;
    s->top = top;
    ScrollSync(frm, s, redraw);
}

/* Scrollbar drags and the hardware page keys.  Returns true if handled. */
static Boolean ScrollEvent(FormPtr frm, EventType *ev, ScrollState *s)
{
    ListPtr lst;

    if (ev->eType == sclRepeatEvent) {
        if (ev->data.sclRepeat.scrollBarID == s->sclID) {
            s->top = ev->data.sclRepeat.newValue;
            lst = (ListPtr)OBJ(frm, s->listID);
            LstSetTopItem(lst, s->top);
            LstDrawList(lst);
        }
        return false;       /* let the scroll bar finish its own update */
    }
    if (ev->eType == keyDownEvent) {
        if (ev->data.keyDown.chr == pageUpChr) {
            s->top -= (s->vis - 1);
            ScrollSync(frm, s, true);
            return true;
        }
        if (ev->data.keyDown.chr == pageDownChr) {
            s->top += (s->vis - 1);
            ScrollSync(frm, s, true);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Row drawing                                                         */
/* ------------------------------------------------------------------ */

static void DrawMainRow(Int16 itemNum, RectangleType *b, Char **itemsText)
{
    Char   name[NAME_LEN], cnt[16], tmp[8];
    UInt16 count, done;
    Int16  w = 0;

    RecInfo(g_map[itemNum], name, &count, &done);
    if (g_view == KIND_LIST) {
        StrIToA(cnt, done);
        StrCat(cnt, "/");
        StrIToA(tmp, count);
        StrCat(cnt, tmp);
    } else {
        StrIToA(cnt, count);
    }
    w = FntCharsWidth(cnt, StrLen(cnt));
    WinDrawChars(cnt, StrLen(cnt), b->topLeft.x + b->extent.x - w - 2, b->topLeft.y);
    WinDrawTruncChars(name, StrLen(name), b->topLeft.x + 2, b->topLeft.y,
                      b->extent.x - w - 8);
}

static void DrawPickRow(Int16 itemNum, RectangleType *b, Char **itemsText)
{
    Char   name[NAME_LEN];
    UInt16 count, done;

    RecInfo(g_pickMap[itemNum], name, &count, &done);
    WinDrawTruncChars(name, StrLen(name), b->topLeft.x + 2, b->topLeft.y,
                      b->extent.x - 4);
}

static void DrawCheckRow(Int16 itemNum, RectangleType *b, Char **itemsText)
{
    RectangleType box;
    Int16 x = b->topLeft.x, y = b->topLeft.y, tw, maxw;
    Char *t = g_list->text[itemNum];

    box.topLeft.x = x + 2;
    box.topLeft.y = y + 1;
    box.extent.x = 9;
    box.extent.y = 9;
    WinDrawRectangleFrame(simpleFrame, &box);

    maxw = b->extent.x - 16;
    WinDrawTruncChars(t, StrLen(t), x + 14, y, maxw);
    if (g_list->done[itemNum]) {
        WinDrawLine(x + 4, y + 3, x + 8, y + 7);
        WinDrawLine(x + 8, y + 3, x + 4, y + 7);
        tw = FntCharsWidth(t, StrLen(t));
        if (tw > maxw)
            tw = maxw;
        WinDrawLine(x + 14, y + 6, x + 14 + tw, y + 6);
    }
}

static void DrawEditRow(Int16 itemNum, RectangleType *b, Char **itemsText)
{
    Char *t = g_list->text[itemNum];

    WinDrawTruncChars(t, StrLen(t), b->topLeft.x + 2, b->topLeft.y,
                      b->extent.x - 4);
}

static void DrawProgress(void)
{
    Char          s[32], n[8];
    UInt16        i, d = 0;
    RectangleType r;

    for (i = 0; i < g_list->count; i++)
        d += g_list->done[i];
    if (g_list->count > 0 && d == g_list->count) {
        StrCopy(s, "All done!");
    } else {
        StrIToA(s, d);
        StrCat(s, "/");
        StrIToA(n, g_list->count);
        StrCat(s, n);
        StrCat(s, " done");
    }
    r.topLeft.x = 100;
    r.topLeft.y = 146;
    r.extent.x = 60;
    r.extent.y = 12;
    WinEraseRectangle(&r, 0);
    WinDrawChars(s, StrLen(s), 102, 147);
}

static void DrawMainHint(void)
{
    const Char *a, *b;

    if (g_mapCount > 0)
        return;
    if (g_view == KIND_LIST) {
        a = "No lists yet. Tap New to start";
        b = "one from a template.";
    } else {
        a = "No templates yet. Tap New to";
        b = "make one, then add items.";
    }
    WinDrawChars(a, StrLen(a), 4, 40);
    WinDrawChars(b, StrLen(b), 4, 52);
}

/* ------------------------------------------------------------------ */
/* Dialogs                                                             */
/* ------------------------------------------------------------------ */

static void TrimCopy(Char *dst, const Char *src, UInt16 maxLen)
{
    UInt16 len;

    while (*src == ' ')
        src++;
    StrNCopy(dst, src, maxLen - 1);
    dst[maxLen - 1] = 0;
    len = StrLen(dst);
    while (len > 0 && dst[len - 1] == ' ')
        dst[--len] = 0;
}

/* Returns btnTextOK / btnTextNext, or 0 if cancelled or empty. */
static UInt16 AskText(const Char *title, const Char *initial, Boolean allowMore)
{
    FormPtr  frm = FrmInitForm(frmText);
    FieldPtr fld = (FieldPtr)OBJ(frm, fldText);
    UInt16   btn;
    Char    *t;

    FrmSetTitle(frm, (Char *)title);
    if (!allowMore)
        FrmHideObject(frm, FrmGetObjectIndex(frm, btnTextNext));
    FrmSetFocus(frm, FrmGetObjectIndex(frm, fldText));
    if (initial && initial[0]) {
        FldInsert(fld, initial, StrLen(initial));
        FldSetSelection(fld, 0, StrLen(initial));
    }
    btn = FrmDoDialog(frm);

    g_textBuf[0] = 0;
    if (btn != btnTextCancel) {
        t = FldGetTextPtr(fld);
        if (t)
            TrimCopy(g_textBuf, t, ITEM_LEN);
    }
    FrmDeleteForm(frm);

    if (btn == btnTextCancel || g_textBuf[0] == 0)
        return 0;
    return btn;
}

static void ShowAbout(void)
{
    FormPtr frm = FrmInitForm(frmAbout);

    FrmDoDialog(frm);
    FrmDeleteForm(frm);
}

static ScrollState g_pickScr;           /* scroll state of the picker dialog */

/* The picker's scroll bar and page keys need a handler while it is open. */
static Boolean PickHandler(EventType *ev)
{
    if (ev->eType == sclRepeatEvent || ev->eType == keyDownEvent)
        return ScrollEvent(FrmGetActiveForm(), ev, &g_pickScr);
    return false;
}

/* Returns the record index of the chosen template, or NEW_REC if none. */
static UInt16 PickTemplate(void)
{
    FormPtr frm = FrmInitForm(frmPick);
    ListPtr lst;
    UInt16  btn, result = NEW_REC;
    Int16   sel;

    g_pickCount = BuildMap(KIND_TEMPLATE, g_pickMap);
    SetupList(frm, &g_pickScr, lstPick, sclPick, g_pickCount, PICK_VIS, 0,
              DrawPickRow, false);
    lst = (ListPtr)OBJ(frm, lstPick);
    if (g_pickCount > 0)
        LstSetSelection(lst, 0);
    FrmSetEventHandler(frm, PickHandler);

    btn = FrmDoDialog(frm);

    sel = LstGetSelection(lst);
    if (btn == btnPickStart && sel >= 0 && sel < (Int16)g_pickCount)
        result = g_pickMap[sel];
    FrmDeleteForm(frm);
    return result;
}

/* ------------------------------------------------------------------ */
/* Actions shared by the forms                                         */
/* ------------------------------------------------------------------ */

/* Adds items to g_list.  OK adds one and stops, More adds and asks again. */
static Boolean AddItems(void)
{
    UInt16  btn;
    Boolean changed = false;

    do {
        if (g_list->count >= MAX_ITEMS) {
            FrmAlert(alertListFull);
            break;
        }
        btn = AskText("Add item", "", true);
        if (btn == 0)
            break;
        StrCopy(g_list->text[g_list->count], g_textBuf);
        g_list->done[g_list->count] = 0;
        g_list->count++;
        changed = true;
    } while (btn == btnTextNext);

    if (changed)
        SaveCurrent();
    return changed;
}

static void RenameCurrent(FormPtr frm)
{
    if (AskText("Rename", g_list->name, false) != 0) {
        StrCopy(g_list->name, g_textBuf);
        SaveCurrent();
        FrmSetTitle(frm, g_list->name);
    }
}

static Boolean DeleteCurrent(void)
{
    if (FrmCustomAlert(alertDelete, g_list->name, "", "") != 0)
        return false;
    DmRemoveRecord(g_db, g_rec);
    return true;
}

static void StartFromTemplate(void)
{
    UInt16 tmpl, rec = NEW_REC, i;

    if (BuildMap(KIND_TEMPLATE, g_pickMap) == 0) {
        FrmAlert(alertNoTemplates);
        return;
    }
    tmpl = PickTemplate();
    if (tmpl == NEW_REC)
        return;
    if (!LoadList(tmpl))
        return;
    if (AskText("Name this list", g_list->name, false) == 0)
        return;
    StrCopy(g_list->name, g_textBuf);
    g_list->kind = KIND_LIST;
    for (i = 0; i < MAX_ITEMS; i++)
        g_list->done[i] = 0;
    if (!SaveList(&rec)) {
        FrmAlert(alertSaveFail);
        return;
    }
    g_rec = rec;
    g_view = KIND_LIST;
    g_return = frmMain;
    FrmGotoForm(frmChecklist);
}

static void NewTemplate(void)
{
    UInt16 rec = NEW_REC;

    if (AskText("New template", "", false) == 0)
        return;
    MemSet(g_list, sizeof(ListData), 0);
    g_list->kind = KIND_TEMPLATE;
    StrCopy(g_list->name, g_textBuf);
    if (!SaveList(&rec)) {
        FrmAlert(alertSaveFail);
        return;
    }
    g_rec = rec;
    g_view = KIND_TEMPLATE;
    g_return = frmMain;
    FrmGotoForm(frmEdit);
}

/* ------------------------------------------------------------------ */
/* Main form                                                           */
/* ------------------------------------------------------------------ */

static void MainSetup(FormPtr frm, Boolean redraw)
{
    g_mapCount = BuildMap(g_view, g_map);
    SetupList(frm, &g_scr, lstMain, sclMain, g_mapCount, MAIN_VIS, 0,
              DrawMainRow, redraw);
    if (redraw)
        DrawMainHint();
}

static Boolean MainHandler(EventType *ev)
{
    FormPtr frm = FrmGetActiveForm();
    Boolean handled = false;
    Int16   sel;

    if (ScrollEvent(frm, ev, &g_scr))
        return true;

    switch (ev->eType) {
    case frmOpenEvent:
    case frmUpdateEvent:
        FrmSetControlGroupSelection(frm, 1,
            (g_view == KIND_LIST) ? btnLists : btnTemplates);
        MainSetup(frm, false);
        FrmDrawForm(frm);
        DrawMainHint();
        handled = true;
        break;

    case ctlSelectEvent:
        switch (ev->data.ctlSelect.controlID) {
        case btnLists:
            g_view = KIND_LIST;
            MainSetup(frm, true);
            handled = true;
            break;
        case btnTemplates:
            g_view = KIND_TEMPLATE;
            MainSetup(frm, true);
            handled = true;
            break;
        case btnNew:
            if (g_view == KIND_LIST)
                StartFromTemplate();
            else
                NewTemplate();
            handled = true;
            break;
        }
        break;

    case lstSelectEvent:
        if (ev->data.lstSelect.listID == lstMain) {
            sel = ev->data.lstSelect.selection;
            if (sel >= 0 && sel < (Int16)g_mapCount && LoadList(g_map[sel])) {
                g_return = frmMain;
                FrmGotoForm(g_view == KIND_LIST ? frmChecklist : frmEdit);
            }
            handled = true;
        }
        break;

    case menuEvent:
        switch (ev->data.menu.itemID) {
        case mnuNewList:
            StartFromTemplate();
            handled = true;
            break;
        case mnuNewTemplate:
            NewTemplate();
            handled = true;
            break;
        case mnuAbout:
            ShowAbout();
            handled = true;
            break;
        }
        break;

    default:
        break;
    }
    return handled;
}

/* ------------------------------------------------------------------ */
/* Checklist form (check things off)                                   */
/* ------------------------------------------------------------------ */

static void CheckSetup(FormPtr frm, Int16 top, Boolean redraw)
{
    SetupList(frm, &g_scr, lstCheck, sclCheck, g_list->count, CHECK_VIS, top,
              DrawCheckRow, redraw);
}

static Boolean CheckHandler(EventType *ev)
{
    FormPtr frm = FrmGetActiveForm();
    ListPtr lst = (ListPtr)OBJ(frm, lstCheck);
    Boolean handled = false;
    Int16   sel;
    UInt16  i;

    if (ScrollEvent(frm, ev, &g_scr))
        return true;

    switch (ev->eType) {
    case frmOpenEvent:
        FrmSetTitle(frm, g_list->name);
        CheckSetup(frm, 0, false);
        FrmDrawForm(frm);
        DrawProgress();
        handled = true;
        break;

    case lstSelectEvent:
        sel = ev->data.lstSelect.selection;
        if (sel >= 0 && sel < g_list->count) {
            g_list->done[sel] = g_list->done[sel] ? 0 : 1;
            SaveCurrent();
        }
        LstSetSelection(lst, noListSelection);
        LstDrawList(lst);
        DrawProgress();
        handled = true;
        break;

    case ctlSelectEvent:
        switch (ev->data.ctlSelect.controlID) {
        case btnCkDone:
            FrmGotoForm(frmMain);
            handled = true;
            break;
        case btnCkAdd:
            if (AddItems()) {
                CheckSetup(frm, 32000, true);
                DrawProgress();
            }
            handled = true;
            break;
        case btnCkEdit:
            g_return = frmChecklist;
            FrmGotoForm(frmEdit);
            handled = true;
            break;
        }
        break;

    case menuEvent:
        switch (ev->data.menu.itemID) {
        case mnuUncheckAll:
            for (i = 0; i < g_list->count; i++)
                g_list->done[i] = 0;
            SaveCurrent();
            LstDrawList(lst);
            DrawProgress();
            handled = true;
            break;
        case mnuEditItems:
            g_return = frmChecklist;
            FrmGotoForm(frmEdit);
            handled = true;
            break;
        case mnuAddItem:
            if (AddItems()) {
                CheckSetup(frm, 32000, true);
                DrawProgress();
            }
            handled = true;
            break;
        case mnuRename:
            RenameCurrent(frm);
            handled = true;
            break;
        case mnuBeam:
            BeamRecord(g_rec);
            handled = true;
            break;
        case mnuDelete:
            if (DeleteCurrent())
                FrmGotoForm(frmMain);
            handled = true;
            break;
        case mnuAbout:
            ShowAbout();
            handled = true;
            break;
        }
        break;

    default:
        break;
    }
    return handled;
}

/* ------------------------------------------------------------------ */
/* Editor form (templates, or the items of a list in progress)         */
/* ------------------------------------------------------------------ */

static void EditSetup(FormPtr frm, Int16 top, Boolean redraw)
{
    SetupList(frm, &g_scr, lstEdit, sclEdit, g_list->count, EDIT_VIS, top,
              DrawEditRow, redraw);
}

static void SwapItems(UInt16 a, UInt16 b)
{
    Char  tmp[ITEM_LEN];
    UInt8 d;

    StrCopy(tmp, g_list->text[a]);
    StrCopy(g_list->text[a], g_list->text[b]);
    StrCopy(g_list->text[b], tmp);
    d = g_list->done[a];
    g_list->done[a] = g_list->done[b];
    g_list->done[b] = d;
}

static Boolean EditHandler(EventType *ev)
{
    FormPtr frm = FrmGetActiveForm();
    ListPtr lst = (ListPtr)OBJ(frm, lstEdit);
    Boolean handled = false;
    Int16   sel, i, to;

    if (ScrollEvent(frm, ev, &g_scr))
        return true;

    switch (ev->eType) {
    case frmOpenEvent:
        FrmSetTitle(frm, g_list->name);
        EditSetup(frm, 0, false);
        FrmDrawForm(frm);
        handled = true;
        break;

    case ctlSelectEvent:
        switch (ev->data.ctlSelect.controlID) {
        case btnEdDone:
            FrmGotoForm(g_return);
            handled = true;
            break;

        case btnEdAdd:
            if (AddItems())
                EditSetup(frm, 32000, true);
            handled = true;
            break;

        case btnEdEdit:
            sel = LstGetSelection(lst);
            if (sel < 0 || sel >= g_list->count) {
                FrmAlert(alertNoSelection);
            } else if (AskText("Edit item", g_list->text[sel], false) != 0) {
                StrCopy(g_list->text[sel], g_textBuf);
                SaveCurrent();
                LstSetSelection(lst, sel);
                LstDrawList(lst);
            }
            handled = true;
            break;

        case btnEdDel:
            sel = LstGetSelection(lst);
            if (sel < 0 || sel >= g_list->count) {
                FrmAlert(alertNoSelection);
            } else {
                for (i = sel; i < g_list->count - 1; i++) {
                    StrCopy(g_list->text[i], g_list->text[i + 1]);
                    g_list->done[i] = g_list->done[i + 1];
                }
                g_list->count--;
                SaveCurrent();
                EditSetup(frm, g_scr.top, true);
            }
            handled = true;
            break;

        case btnEdUp:
        case btnEdDown:
            sel = LstGetSelection(lst);
            to = (ev->data.ctlSelect.controlID == btnEdUp) ? sel - 1 : sel + 1;
            if (sel < 0 || sel >= g_list->count) {
                FrmAlert(alertNoSelection);
            } else if (to >= 0 && to < g_list->count) {
                SwapItems(sel, to);
                SaveCurrent();
                if (to < g_scr.top)
                    g_scr.top = to;
                else if (to >= g_scr.top + EDIT_VIS)
                    g_scr.top = to - EDIT_VIS + 1;
                ScrollSync(frm, &g_scr, false);
                LstSetSelection(lst, to);
                LstDrawList(lst);
            }
            handled = true;
            break;
        }
        break;

    case menuEvent:
        switch (ev->data.menu.itemID) {
        case mnuAddItem:
            if (AddItems())
                EditSetup(frm, 32000, true);
            handled = true;
            break;
        case mnuRename:
            RenameCurrent(frm);
            handled = true;
            break;
        case mnuBeam:
            BeamRecord(g_rec);
            handled = true;
            break;
        case mnuDelete:
            if (DeleteCurrent())
                FrmGotoForm(frmMain);
            handled = true;
            break;
        case mnuAbout:
            ShowAbout();
            handled = true;
            break;
        }
        break;

    default:
        break;
    }
    return handled;
}

/* ------------------------------------------------------------------ */
/* Application plumbing                                                */
/* ------------------------------------------------------------------ */

static Boolean ApplicationHandleEvent(EventType *ev)
{
    FormPtr frm;
    UInt16  id;

    if (ev->eType == frmLoadEvent) {
        id = ev->data.frmLoad.formID;
        frm = FrmInitForm(id);
        FrmSetActiveForm(frm);
        switch (id) {
        case frmMain:      FrmSetEventHandler(frm, MainHandler);  break;
        case frmChecklist: FrmSetEventHandler(frm, CheckHandler); break;
        case frmEdit:      FrmSetEventHandler(frm, EditHandler);  break;
        }
        return true;
    }
    return false;
}

static void AppEventLoop(void)
{
    EventType ev;
    UInt16    err;

    do {
        EvtGetEvent(&ev, evtWaitForever);
        if (!SysHandleEvent(&ev))
            if (!MenuHandleEvent(0, &ev, &err))
                if (!ApplicationHandleEvent(&ev))
                    FrmDispatchEvent(&ev);
    } while (ev.eType != appStopEvent);
}

static Err StartApplication(void)
{
    Boolean created = false;

    g_list = (ListData *)MemPtrNew(sizeof(ListData));
    if (!g_list)
        return memErrNotEnoughSpace;
    MemSet(g_list, sizeof(ListData), 0);

    g_db = DmOpenDatabaseByTypeCreator(DB_TYPE, CREATOR, dmModeReadWrite);
    if (!g_db) {
        if (DmCreateDatabase(0, DB_NAME, CREATOR, DB_TYPE, false) != errNone)
            return DmGetLastErr();
        g_db = DmOpenDatabaseByTypeCreator(DB_TYPE, CREATOR, dmModeReadWrite);
        created = true;
    }
    if (!g_db)
        return DmGetLastErr();

    if (created)
        AddStarterTemplate();

    FtrSet(CREATOR, 0, 1);              /* "app is running" marker for beaming */
    RegisterBeaming();
    return errNone;
}

static void StopApplication(void)
{
    FtrUnregister(CREATOR, 0);
    FrmCloseAllForms();
    if (g_db)
        DmCloseDatabase(g_db);
    if (g_list)
        MemPtrFree(g_list);
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
    Err err;

    switch (cmd) {
    case sysAppLaunchCmdNormalLaunch:
        err = StartApplication();
        if (err != errNone)
            return err;
        FrmGotoForm(frmMain);
        AppEventLoop();
        StopApplication();
        break;

    case sysAppLaunchCmdSyncNotify:
    case sysAppLaunchCmdSystemReset:
        RegisterBeaming();
        break;

    case sysAppLaunchCmdExgReceiveData:
        {
            UInt8   kind;
            UInt32  running = 0;
            Boolean live;

            live = (launchFlags & sysAppLaunchFlagSubCall) &&
                   FtrGet(CREATOR, 0, &running) == errNone && running;
            err = ReceiveBeam((ExgSocketPtr)cmdPBP, live ? g_db : 0, &kind);
            if (live && err == errNone && kind <= KIND_TEMPLATE) {
                g_view = kind;
                if (FrmGetFormId(FrmGetActiveForm()) == frmMain)
                    FrmUpdateForm(frmMain, frmRedrawUpdateCode);
            }
        }
        break;

    default:
        break;
    }
    return errNone;
}

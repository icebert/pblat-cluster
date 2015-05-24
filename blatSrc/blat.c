/* blat - Standalone BLAT fast sequence search command line tool. */
/* Copyright 2001-2004 Jim Kent.  All rights reserved. */

/* Modified by Meng Wang. 2012-2014 */

#include "common.h"
#include "memalloc.h"
#include "linefile.h"
#include "bits.h"
#include "hash.h"
#include "dnautil.h"
#include "dnaseq.h"
#include "fa.h"
#include "nib.h"
#include "twoBit.h"
#include "psl.h"
#include "sig.h"
#include "options.h"
#include "obscure.h"
#include "genoFind.h"
#include "trans3.h"
#include "gfClientLib.h"

#include <sys/types.h>
#include <pthread.h>
#include <mpi.h>



/* Variables shared with other modules.  Set in this module, read only
 * elsewhere. */
char *databaseName;		/* File name of database. */
int databaseSeqCount = 0;	/* Number of sequences in database. */
unsigned long databaseLetters = 0;	/* Number of bases in database. */

enum constants {
    qWarnSize = 5000000, /* Warn if more than this many bases in one query. */
};

/* Variables that can be set from command line. */
int threads = 1;
int tileSize = 11;
int stepSize = 0;	/* Default (same as tileSize) */
int minMatch = 2;
int minScore = 30;
int maxGap = 2;
int repMatch = 1024*4;
int dotEvery = 0;
boolean oneOff = FALSE;
boolean noHead = FALSE;
boolean trimA = FALSE;
boolean trimHardA = FALSE;
boolean trimT = FALSE;
boolean fastMap = FALSE;
char *makeOoc = NULL;
char *ooc = NULL;
enum gfType qType = gftDna;
enum gfType tType = gftDna;
char *mask = NULL;
char *repeats = NULL;
char *qMask = NULL;
double minRepDivergence = 15;
double minIdentity = 90;
char *outputFormat = "psl";


void usage()
/* Explain usage and exit. */
{
    printf(
        "pblat-cluster - BLAT with cluster parallel hybrid computing support\n"
        "                v. %s fast sequence search command line tool\n"
        "\n"
        "usage:\n"
        "   mpirun -n <N> pblat-cluster database query [-ooc=11.ooc] output.psl\n"
        "where:\n"
        "   database and query are each a .fa file\n"
        "   -ooc=11.ooc tells the program to load over-occurring 11-mers from\n"
        "               and external file.  This will increase the speed\n"
        "               by a factor of 40 in many cases, but is not required\n"
        "   output.psl is where to put the output.\n"
        "\n"
        "options:\n"
        "   -t=type     Database type.  Type is one of:\n"
        "                 dna - DNA sequence\n"
        "                 prot - protein sequence\n"
        "                 dnax - DNA sequence translated in six frames to protein\n"
        "               The default is dna\n"
        "   -q=type     Query type.  Type is one of:\n"
        "                 dna - DNA sequence\n"
        "                 rna - RNA sequence\n"
        "                 prot - protein sequence\n"
        "                 dnax - DNA sequence translated in six frames to protein\n"
        "                 rnax - DNA sequence translated in three frames to protein\n"
        "               The default is dna\n"
        "   -prot       Synonymous with -t=prot -q=prot\n"
        "   -ooc=N.ooc  Use overused tile file N.ooc.  N should correspond to \n"
        "               the tileSize\n"
        "   -tileSize=N sets the size of match that triggers an alignment.  \n"
        "               Usually between 8 and 12\n"
        "               Default is 11 for DNA and 5 for protein.\n"
        "   -stepSize=N spacing between tiles. Default is tileSize.\n"
        "   -oneOff=N   If set to 1 this allows one mismatch in tile and still\n"
        "               triggers an alignments.  Default is 0.\n"
        "   -minMatch=N sets the number of tile matches.  Usually set from 2 to 4\n"
        "               Default is 2 for nucleotide, 1 for protein.\n"
        "   -minScore=N sets minimum score.  This is the matches minus the \n"
        "               mismatches minus some sort of gap penalty.  Default is 30\n"
        "   -minIdentity=N Sets minimum sequence identity (in percent).  Default is\n"
        "               90 for nucleotide searches, 25 for protein or translated\n"
        "               protein searches.\n"
        "   -maxGap=N   sets the size of maximum gap between tiles in a clump.  Usually\n"
        "               set from 0 to 3.  Default is 2. Only relevent for minMatch > 1.\n"
        "   -noHead     suppress .psl header (so it's just a tab-separated file)\n"
        "   -makeOoc=N.ooc Make overused tile file. Target needs to be complete genome.\n"
        "   -repMatch=N sets the number of repetitions of a tile allowed before\n"
        "               it is marked as overused.  Typically this is 256 for tileSize\n"
        "               12, 1024 for tile size 11, 4096 for tile size 10.\n"
        "               Default is 1024.  Typically only comes into play with makeOoc.\n"
        "               Also affected by stepSize. When stepSize is halved repMatch is\n"
        "               doubled to compensate.\n"
        "   -mask=type  Mask out repeats.  Alignments won't be started in masked region\n"
        "               but may extend through it in nucleotide searches.  Masked areas\n"
        "               are ignored entirely in protein or translated searches. Types are\n"
        "                 lower - mask out lower cased sequence\n"
        "                 upper - mask out upper cased sequence\n"
        "                 out   - mask according to database.out RepeatMasker .out file\n"
        "                 file.out - mask database according to RepeatMasker file.out\n"
        "   -qMask=type Mask out repeats in query sequence.  Similar to -mask above but\n"
        "               for query rather than target sequence.\n"
        "   -repeats=type Type is same as mask types above.  Repeat bases will not be\n"
        "               masked in any way, but matches in repeat areas will be reported\n"
        "               separately from matches in other areas in the psl output.\n"
        "   -minRepDivergence=NN - minimum percent divergence of repeats to allow \n"
        "               them to be unmasked.  Default is 15.  Only relevant for \n"
        "               masking using RepeatMasker .out files.\n"
        "   -dots=N     Output dot every N sequences to show program's progress\n"
        "   -trimT      Trim leading poly-T\n"
        "   -noTrimA    Don't trim trailing poly-A\n"
        "   -trimHardA  Remove poly-A tail from qSize as well as alignments in \n"
        "               psl output\n"
        "   -fastMap    Run for fast DNA/DNA remapping - not allowing introns, \n"
        "               requiring high %%ID. Query sizes must not exceed %d.\n"
        "   -out=type   Controls output file format.  Type is one of:\n"
        "                   psl - Default.  Tab separated format, no sequence\n"
        "                   pslx - Tab separated format with sequence\n"
        "                   axt - blastz-associated axt format\n"
        "                   maf - multiz-associated maf format\n"
        "                   sim4 - similar to sim4 format\n"
        "                   wublast - similar to wublast format\n"
        "                   blast - similar to NCBI blast format\n"
        "                   blast8- NCBI blast tabular format\n"
        "                   blast9 - NCBI blast tabular format with comments\n"
        "   -fine       For high quality mRNAs look harder for small initial and\n"
        "               terminal exons.  Not recommended for ESTs\n"
        "   -maxIntron=N  Sets maximum intron size. Default is %d\n"
        "   -extendThroughN - Allows extension of alignment through large blocks of N's\n"
        , gfVersion, MAXSINGLEPIECESIZE, ffIntronMaxDefault
    );
    exit(-1);
}


struct optionSpec options[] = {
    {"t", OPTION_STRING},
    {"q", OPTION_STRING},
    {"prot", OPTION_BOOLEAN},
    {"ooc", OPTION_STRING},
    {"tileSize", OPTION_INT},
    {"stepSize", OPTION_INT},
    {"oneOff", OPTION_INT},
    {"minMatch", OPTION_INT},
    {"minScore", OPTION_INT},
    {"minIdentity", OPTION_FLOAT},
    {"maxGap", OPTION_INT},
    {"noHead", OPTION_BOOLEAN},
    {"makeOoc", OPTION_STRING},
    {"repMatch", OPTION_INT},
    {"mask", OPTION_STRING},
    {"qMask", OPTION_STRING},
    {"repeats", OPTION_STRING},
    {"minRepDivergence", OPTION_FLOAT},
    {"dots", OPTION_INT},
    {"trimT", OPTION_BOOLEAN},
    {"noTrimA", OPTION_BOOLEAN},
    {"trimHardA", OPTION_BOOLEAN},
    {"fastMap", OPTION_BOOLEAN},
    {"out", OPTION_STRING},
    {"fine", OPTION_BOOLEAN},
    {"maxIntron", OPTION_INT},
    {"extendThroughN", OPTION_BOOLEAN},
    {NULL, 0},
};




void searchOneStrand(struct dnaSeq *seq, struct genoFind *gf, FILE *psl,
                     boolean isRc, struct hash *maskHash, Bits *qMaskBits, struct gfOutput *gvo)
/* Search for seq in index, align it, and write results to psl. */
{
    if (fastMap && (seq->size > MAXSINGLEPIECESIZE))
        errAbort("Maximum single piece size (%d) exceeded by query %s of size (%d). "
        "Larger pieces will have to be split up until no larger than this limit "
        "when the -fastMap option is used."	
        , MAXSINGLEPIECESIZE, seq->name, seq->size);
    
    gfLongDnaInMem(seq, gf, isRc, minScore, qMaskBits, gvo, fastMap, optionExists("fine"));
}


void searchOneProt(aaSeq *seq, struct genoFind *gf, FILE *f, struct gfOutput *gvo)
/* Search for protein seq in index and write results to psl. */
{
    int hitCount;
    struct lm *lm = lmInit(0);
    struct gfClump *clumpList = gfFindClumps(gf, seq, lm, &hitCount);
    gfAlignAaClumps(gf, clumpList, seq, FALSE, minScore, gvo);
    gfClumpFreeList(&clumpList);
    lmCleanup(&lm);
}

void dotOut()
/* Put out a dot every now and then if user want's to. */
{
    static int mod = 1;
    if (dotEvery > 0)
    {
        if (--mod <= 0)
        {
            fputc('.', stdout);
            fflush(stdout);
            mod = dotEvery;
        }
    }
}

void searchOne(bioSeq *seq, struct genoFind *gf, FILE *f, boolean isProt,
               struct hash *maskHash, Bits *qMaskBits, struct gfOutput *gvo)
/* Search for seq on either strand in index. */
{
    dotOut();
    if (isProt)
    {
        searchOneProt(seq, gf, f, gvo);
    }
    else
    {
        gvo->maskHash = maskHash;
        searchOneStrand(seq, gf, f, FALSE, maskHash, qMaskBits, gvo);
        reverseComplement(seq->dna, seq->size);
        searchOneStrand(seq, gf, f, TRUE, maskHash, qMaskBits, gvo);
        reverseComplement(seq->dna, seq->size);
    }
    gfOutputQuery(gvo, f);
}

void trimSeq(struct dnaSeq *seq, struct dnaSeq *trimmed)
/* Copy seq to trimmed (shallow copy) and optionally trim
 * off polyA tail or polyT head. */
{
    DNA *dna = seq->dna;
    int size = seq->size;
    *trimmed = *seq;
    if (trimT)
        maskHeadPolyT(dna, size);
    if (trimA || trimHardA)
    {
        int trimSize = maskTailPolyA(dna, size);
        if (trimHardA)
        {
            trimmed->size -= trimSize;
            dna[size-trimSize] = 0;
        }
    }
}


Bits *maskQuerySeq(struct dnaSeq *seq, boolean isProt,
                   boolean maskQuery, boolean lcMask)
/* Massage query sequence a bit, converting it to correct
 * case (upper for protein/lower for DNA) and optionally
 * returning upper/lower case info , and trimming poly A. */
{
    Bits *qMaskBits = NULL;
    verbose(2, "%s\n", seq->name);
    if (isProt)
        faToProtein(seq->dna, seq->size);
    else
    {
        if (maskQuery)
        {
            if (lcMask)
                toggleCase(seq->dna, seq->size);
            qMaskBits = maskFromUpperCaseSeq(seq);
        }
        faToDna(seq->dna, seq->size);
    }
    if (seq->size > qWarnSize)
    {
        warn("Query sequence %s has size %d, it might take a while.",
             seq->name, seq->size);
    }
    return qMaskBits;
}

void searchOneMaskTrim(struct dnaSeq *seq, boolean isProt,
                       struct genoFind *gf, FILE *outFile,
                       struct hash *maskHash,
                       long long *retTotalSize, int *retCount,
                       struct gfOutput *gvo)
/* Search a single sequence against a single genoFind index. */
{
    boolean maskQuery = (qMask != NULL);
    boolean lcMask = (qMask != NULL && sameWord(qMask, "lower"));
    Bits *qMaskBits = maskQuerySeq(seq, isProt, maskQuery, lcMask);
    struct dnaSeq trimmedSeq;
    ZeroVar(&trimmedSeq);
    trimSeq(seq, &trimmedSeq);
    if (qType == gftRna || qType == gftRnaX)
        memSwapChar(trimmedSeq.dna, trimmedSeq.size, 'u', 't');
    searchOne(&trimmedSeq, gf, outFile, isProt, maskHash, qMaskBits, gvo);
    *retTotalSize += seq->size;
    *retCount += 1;
    bitFree(&qMaskBits);
}


void* performSearch(void* args)
{
    int             id=*((int*)(((void**)args)[0]));
    int             queryCount=*((int*)(((void**)args)[1]));
    char            **files=(char**)(((void**)args)[2]);
    struct lineFile *lf=(struct lineFile *)(((void**)args)[3]);
    struct genoFind *gf=(struct genoFind *)(((void**)args)[4]);
    boolean         isProt=*((boolean*)(((void**)args)[5]));
    struct hash     *maskHash=(struct hash *)(((void**)args)[6]);
    FILE            *outFile=(FILE *)(((void**)args)[7]);
    boolean         showStatus=*((boolean*)(((void**)args)[8]));
    struct gfOutput *gvo=(struct gfOutput *)(((void**)args)[9]);

    int             i;
    char            *fileName;
    int             count = 0;
    long long   totalSize = 0;

    unsigned faFastBufSize = 0;
    DNA *faFastBuf;


    if (id==0)
        gfOutputHead(gvo, outFile);
//for (i=0; i<queryCount; ++i)
    {
        fileName = files[0];
        if (nibIsFile(fileName))
        {
            struct dnaSeq *seq;

            if (isProt)
                errAbort("%s: Can't use .nib files with -prot or d=prot option\n", fileName);
            seq = nibLoadAllMasked(NIB_MASK_MIXED, fileName);
            freez(&seq->name);
            seq->name = cloneString(fileName);
            searchOneMaskTrim(seq, isProt, gf, outFile,
                              maskHash, &totalSize, &count, gvo);
            freeDnaSeq(&seq);
        }
        else if (twoBitIsSpec(fileName))
        {
            struct twoBitSpec *tbs = twoBitSpecNew(fileName);
            struct twoBitFile *tbf = twoBitOpen(tbs->fileName);
            if (isProt)
                errAbort("%s is a two bit file, which doesn't work for proteins.",
                         fileName);
            if (tbs->seqs != NULL)
            {
                struct twoBitSeqSpec *ss = NULL;
                for (ss = tbs->seqs;  ss != NULL;  ss = ss->next)
                {
                    struct dnaSeq *seq = twoBitReadSeqFrag(tbf, ss->name,
                                                           ss->start, ss->end);
                    searchOneMaskTrim(seq, isProt, gf, outFile,
                                      maskHash, &totalSize, &count, gvo);
                    dnaSeqFree(&seq);
                }
            }
            else
            {
                struct twoBitIndex *index = NULL;
                for (index = tbf->indexList; index != NULL; index = index->next)
                {
                    struct dnaSeq *seq = twoBitReadSeqFrag(tbf, index->name, 0, 0);
                    searchOneMaskTrim(seq, isProt, gf, outFile,
                                      maskHash, &totalSize, &count, gvo);
                    dnaSeqFree(&seq);
                }
            }
            twoBitClose(&tbf);
        }
        else
        {
            struct dnaSeq seq;
            seq.name=(char*)malloc(sizeof(char)*512);
            while (queryCount-- && faMixedSpeedReadNext(lf, &seq.dna, &seq.size, &seq.name, &faFastBuf, &faFastBufSize))
            {
                searchOneMaskTrim(&seq, isProt, gf, outFile,
                                  maskHash, &totalSize, &count, gvo);
            }
            free(seq.name);
            faFreeFastBuf(&faFastBuf, &faFastBufSize);
        }
    }
    if (showStatus)
        printf("Searched %lld bases in %d sequences\n", totalSize, count);
}

void searchOneIndex(int fileCount, char *files[], struct lineFile *lf[], struct genoFind *gf,
                    boolean isProt, struct hash *maskHash, FILE *out[], struct gfOutput *gvo[],
                    boolean showStatus)
/* Search all sequences in all files against single genoFind index. */
{
    int        i;
    pthread_t* thd=(pthread_t*)malloc(sizeof(pthread_t)*threads);
    void***    args=(void***)malloc(sizeof(void*)*threads);
    int*       id=(int*)malloc(sizeof(int)*threads);

    for (i=0; i<threads; i++)
    {
        args[i]=(void**)malloc(sizeof(void*)*10);
        args[i][1]=&fileCount;
        args[i][2]=files;
        args[i][4]=gf;
        args[i][5]=&isProt;
        args[i][6]=maskHash;
        args[i][8]=&showStatus;

        id[i]=i;
        args[i][0]=&(id[i]);
        args[i][3]=lf[i];
        args[i][7]=out[i];
        args[i][9]=gvo[i];
        if (pthread_create(&(thd[i]), NULL, performSearch, (void*)(args[i])) != 0)
        {
            printf("Failed to create threads\n");
            return;
        }
    }

    for (i=0; i<threads; i++)
        pthread_join(thd[i], NULL);
    free(thd);
    for (i=0; i<threads; i++)
        free(args[i]);
    free(args);
    free(id);
}

struct trans3 *seqListToTrans3List(struct dnaSeq *seqList, aaSeq *transLists[3], struct hash **retHash)
/* Convert sequence list to a trans3 list and lists for each of three frames. */
{
    int frame;
    struct dnaSeq *seq;
    struct trans3 *t3List = NULL, *t3;
    struct hash *hash = newHash(0);

    for (seq = seqList; seq != NULL; seq = seq->next)
    {
        t3 = trans3New(seq);
        hashAddUnique(hash, t3->name, t3);
        slAddHead(&t3List, t3);
        for (frame = 0; frame < 3; ++frame)
        {
            slAddHead(&transLists[frame], t3->trans[frame]);
        }
    }
    slReverse(&t3List);
    for (frame = 0; frame < 3; ++frame)
    {
        slReverse(&transLists[frame]);
    }
    *retHash = hash;
    return t3List;
}

void tripleSearch(aaSeq *qSeq, struct genoFind *gfs[3], struct hash *t3Hash, boolean dbIsRc,
                  FILE *f, struct gfOutput *gvo)
/* Look for qSeq in indices for three frames.  Then do rest of alignment. */
{
    gvo->reportTargetStrand = TRUE;
    gfFindAlignAaTrans(gfs, qSeq, t3Hash, dbIsRc, minScore, gvo);
}

void transTripleSearch(struct dnaSeq *qSeq, struct genoFind *gfs[3], struct hash *t3Hash,
                       boolean dbIsRc, boolean qIsDna, FILE *f, struct gfOutput *gvo)
/* Translate qSeq three ways and look for each in three frames of index. */
{
    int qIsRc;
    gvo->reportTargetStrand = TRUE;
    for (qIsRc = 0; qIsRc <= qIsDna; qIsRc += 1)
    {
        gfLongTransTransInMem(qSeq, gfs, t3Hash, qIsRc, dbIsRc, !qIsDna, minScore, gvo);
        if (qIsDna)
            reverseComplement(qSeq->dna, qSeq->size);
    }
}


void* performBigblat(void* args)
{
    int             id=*((int*)(((void**)args)[0]));
    int             queryCount=*((int*)(((void**)args)[1]));
    char            **queryFiles=(char**)(((void**)args)[2]);
    struct lineFile *lf=(struct lineFile *)(((void**)args)[3]);
    struct genoFind **gfs=(struct genoFind**)(((void**)args)[4]);
    struct hash     *t3Hash=(struct hash*)(((void**)args)[5]);
    int             isRc=*((int*)(((void**)args)[6]));
    boolean         qIsDna=*((boolean*)(((void**)args)[7]));
    FILE            *out=(FILE*)(((void**)args)[8]);
    boolean         transQuery=*((boolean*)(((void**)args)[9]));
    boolean         forceLower=*((boolean*)(((void**)args)[10]));
    boolean         forceUpper=*((boolean*)(((void**)args)[11]));
    boolean         maskUpper=*((boolean*)(((void**)args)[12]));
    boolean         toggle=*((boolean*)(((void**)args)[13]));
    struct gfOutput *gvo=(struct gfOutput *)(((void**)args)[14]);

    int             i;
    struct dnaSeq   trimmedSeq;

    unsigned        faFastBufSize = 0;
    DNA             *faFastBuf;

    ZeroVar(&trimmedSeq);
//for (i=0; i<queryCount; ++i)
    {
        aaSeq qSeq;
        qSeq.name=(char*)malloc(sizeof(char)*512);

        while (queryCount-- && faMixedSpeedReadNext(lf, &qSeq.dna, &qSeq.size, &qSeq.name, &faFastBuf, &faFastBufSize))
        {
            dotOut();
            /* Put it into right case and optionally mask on case. */
            if (forceLower)
                toLowerN(qSeq.dna, qSeq.size);
            else if (forceUpper)
                toUpperN(qSeq.dna, qSeq.size);
            else if (maskUpper)
            {
                if (toggle)
                    toggleCase(qSeq.dna, qSeq.size);
                upperToN(qSeq.dna, qSeq.size);
            }
            if (qSeq.size > qWarnSize)
            {
                warn("Query sequence %s has size %d, it might take a while.",
                     qSeq.name, qSeq.size);
            }
            trimSeq(&qSeq, &trimmedSeq);
            if (transQuery)
                transTripleSearch(&trimmedSeq, gfs, t3Hash, isRc, qIsDna, out, gvo);
            else
                tripleSearch(&trimmedSeq, gfs, t3Hash, isRc, out, gvo);
            gfOutputQuery(gvo, out);
        }
        free(qSeq.name);
        faFreeFastBuf(&faFastBuf, &faFastBufSize);
    }
}

void bigBlat(struct dnaSeq *untransList, int queryCount, char *queryFiles[], struct lineFile *lf[], boolean transQuery,
             boolean qIsDna, FILE *out[], struct gfOutput *gvo[], boolean showStatus)
/* Run query against translated DNA database (3 frames on each strand). */
{
    int             frame, i;
    struct dnaSeq   *seq;
    struct genoFind *gfs[3];
    aaSeq           *dbSeqLists[3];
    struct trans3   *t3List = NULL;
    int             isRc;
    struct hash     *t3Hash = NULL;
    boolean         forceUpper = FALSE;
    boolean         forceLower = FALSE;
    boolean         toggle = FALSE;
    boolean         maskUpper = FALSE;

    pthread_t*      thd=(pthread_t*)malloc(sizeof(pthread_t)*threads);
    void***         args=(void***)malloc(sizeof(void*)*threads);
    int*            id=(int*)malloc(sizeof(int)*threads);


    if (showStatus)
        printf("Blatx %d sequences in database, %d files in query\n", slCount(untransList), queryCount);

    /* Figure out how to manage query case.  Proteins want to be in
     * upper case, generally, nucleotides in lower case.  But there
     * may be repeatMasking based on case as well. */
    if (transQuery)
    {
        if (qMask == NULL)
            forceLower = TRUE;
        else
        {
            maskUpper = TRUE;
            toggle = !sameString(qMask, "upper");
        }
    }
    else
    {
        forceUpper = TRUE;
    }

    if (gvo[0]->fileHead != NULL)
        gvo[0]->fileHead(gvo[0], out[0]);

    for (isRc = FALSE; isRc <= 1; ++isRc)
    {
        /* Initialize local pointer arrays to NULL to prevent surprises. */
        for (frame = 0; frame < 3; ++frame)
        {
            gfs[frame] = NULL;
            dbSeqLists[frame] = NULL;
        }

        t3List = seqListToTrans3List(untransList, dbSeqLists, &t3Hash);
        for (frame = 0; frame < 3; ++frame)
        {
            gfs[frame] = gfIndexSeq(dbSeqLists[frame], minMatch, maxGap, tileSize,
                                    repMatch, ooc, TRUE, oneOff, FALSE, stepSize);
        }

        /* multi-threads */
        for (i=0; i<threads; i++)
        {
            args[i]=(void**)malloc(sizeof(void*)*15);
            args[i][1]=&queryCount;
            args[i][2]=queryFiles;
            args[i][4]=gfs;
            args[i][5]=t3Hash;
            args[i][6]=&isRc;
            args[i][7]=&qIsDna;

            args[i][9]=&transQuery;
            args[i][10]=&forceLower;
            args[i][11]=&forceUpper;
            args[i][12]=&maskUpper;
            args[i][13]=&toggle;

            id[i]=i;
            args[i][0]=&(id[i]);
            args[i][3]=lf[i];
            args[i][8]=out[i];
            args[i][14]=gvo[i];
            if (pthread_create(&(thd[i]), NULL, performBigblat, (void*)(args[i])) != 0)
            {
                printf("Failed to create threads\n");
                return;
            }
        }

        for (i=0; i<threads; i++)
            pthread_join(thd[i], NULL);
        free(thd);
        for (i=0; i<threads; i++)
            free(args[i]);
        free(args);
        free(id);


        /* Clean up time. */
        trans3FreeList(&t3List);
        freeHash(&t3Hash);
        for (frame = 0; frame < 3; ++frame)
        {
            genoFindFree(&gfs[frame]);
        }

        for (seq = untransList; seq != NULL; seq = seq->next)
        {
            reverseComplement(seq->dna, seq->size);
        }
    }
}


void blat(char *dbFile, int queryCount, char **queryFiles, struct lineFile **lf, FILE *out[])
/* blat - Standalone BLAT fast sequence search command line tool. */
{
    char **dbFiles;
    int dbCount;
    struct dnaSeq *dbSeqList, *seq;
    struct genoFind *gf;
    boolean tIsProt = (tType == gftProt);
    boolean qIsProt = (qType == gftProt);
    boolean bothSimpleNuc = (tType == gftDna && (qType == gftDna || qType == gftRna));
    boolean bothSimpleProt = (tIsProt && qIsProt);
    boolean showStatus = (out[0] != stdout);
    /* Stuff to support various output formats. */
    struct gfOutput **gvo;		/* output controller */
    int i;

    databaseName = dbFile;
    gfClientFileArray(dbFile, &dbFiles, &dbCount);
    if (makeOoc != NULL)
    {
        gfMakeOoc(makeOoc, dbFiles, dbCount, tileSize, repMatch, tType);
        if (showStatus)
            printf("Done making %s\n", makeOoc);
        exit(0);
    }

    dbSeqList = gfClientSeqList(dbCount, dbFiles, tIsProt, tType == gftDnaX, repeats,
                                minRepDivergence, showStatus);
    databaseSeqCount = slCount(dbSeqList);
    for (seq = dbSeqList; seq != NULL; seq = seq->next)
        databaseLetters += seq->size;


    gvo = (struct gfOutput **)malloc(sizeof(struct gfOutput *) * threads);
    for (i=0; i<threads; i++)
    {
        gvo[i] = gfOutputAny(outputFormat, minIdentity*10, qIsProt, tIsProt, noHead,
                             databaseName, databaseSeqCount, databaseLetters, minIdentity, out[i]);
    }


    if (bothSimpleNuc || bothSimpleProt)
    {
        struct hash *maskHash = NULL;

        /* Save away masking info for output. */
        if (repeats != NULL)
        {
            maskHash = newHash(0);
            for (seq = dbSeqList; seq != NULL; seq = seq->next)
            {
                Bits *maskedBits = maskFromUpperCaseSeq(seq);
                hashAdd(maskHash, seq->name, maskedBits);
            }
        }

        /* Handle masking and indexing.  If masking is off, we want the indexer
         * to see unmasked sequence, otherwise we want it to see masked.  However
         * after indexing we always want it unmasked, because things are always
         * unmasked for the extension phase. */
        if (mask == NULL && !bothSimpleProt)
            gfClientUnmask(dbSeqList);
        gf = gfIndexSeq(dbSeqList, minMatch, maxGap, tileSize, repMatch, ooc,
                        tIsProt, oneOff, FALSE, stepSize);
        if (mask != NULL)
            gfClientUnmask(dbSeqList);

        searchOneIndex(queryCount, queryFiles, lf, gf, tIsProt, maskHash, out, gvo, showStatus);
        freeHash(&maskHash);
    }
    else if (tType == gftDnaX && qType == gftProt)
    {
        bigBlat(dbSeqList, queryCount, queryFiles, lf, FALSE, TRUE, out, gvo, showStatus);
    }
    else if (tType == gftDnaX && (qType == gftDnaX || qType == gftRnaX))
    {
        bigBlat(dbSeqList, queryCount, queryFiles, lf, TRUE, qType == gftDnaX, out, gvo, showStatus);
    }
    else
    {
        errAbort("Unrecognized combination of target and query types\n");
    }
    if (dotEvery > 0)
        printf("\n");
    freeDnaSeqList(&dbSeqList);
    free(gvo);
}


struct ranknode
{
    int rank;
    struct ranknode *next;
};

struct headnode
{
    char nodename[MPI_MAX_PROCESSOR_NAME];
    struct ranknode *rlist;
    struct headnode *next;
};



struct headnode *findNode(struct headnode *nodelist, char *name)
{
    struct headnode *p;
    for (p=nodelist; p!=NULL; p=p->next)
        if (strcmp(p->nodename, name) == 0)
            break;
    return p;
}


int main(int argc, char *argv[])
/* Process command line into global variables and call blat. */
{
    boolean tIsProtLike, qIsProtLike;
    char buf[1024*64];
    char **queryFiles;
    FILE **out;
    FILE *fres;
    FILE *ftmp;
    struct lineFile **lf;
    int  queryCount;
    int  i, cnt, tmp;

    unsigned faFastBufSize = 0;
    DNA      *faFastBuf;
    
    int  id, chooseid, numproc;
    int  namelen, provided;
    char nodename[MPI_MAX_PROCESSOR_NAME];
    char namebuf[1024*64];
    struct headnode *nodelist;
    struct headnode *pn;
    struct headnode *pnt;
    struct ranknode *pr;
    struct ranknode *prt;
    int    base;
    long long int   *offsets;
    


    /* MPI initialization */
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED)
        MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    MPI_Comm_size(MPI_COMM_WORLD, &numproc);
    MPI_Get_processor_name(nodename, &namelen);
    
    if (id != 0)
    {
        /* For non master process, send its node name
         * to master process */
        MPI_Send(nodename, namelen+1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        
        MPI_Recv(&chooseid, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        if (chooseid == id)
        {
            MPI_Recv(&threads, 1, MPI_INT, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&base,    1, MPI_INT, 0, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        else
        {
            MPI_Finalize();
            return 0;
        }
    }
    else
    {
        /* for MASTER process, identify different nodes and combine
         * multiple processes in the same node into one process using
         * multiple threads. This would minimize the memory usages per
         * node */
        nodelist = (struct headnode *)malloc(sizeof(struct headnode));
        strcpy(nodelist->nodename, nodename);
        nodelist->next  = NULL;
        nodelist->rlist = (struct ranknode *)malloc(sizeof(struct ranknode));
        nodelist->rlist->rank = 0;
        nodelist->rlist->next = NULL;
        
        for (i=1; i<numproc; i++)
        {
            MPI_Recv(namebuf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            pn = findNode(nodelist, namebuf);
            if (pn == NULL)
            {
                for (pn=nodelist;pn->next!=NULL;pn=pn->next);
                pn->next = (struct headnode *)malloc(sizeof(struct headnode));
                pn=pn->next;
                strcpy(pn->nodename, namebuf);
                pn->next  = NULL;
                pn->rlist = (struct ranknode *)malloc(sizeof(struct ranknode));
                pn->rlist->rank = i;
                pn->rlist->next = NULL;
            }
            else
            {
                for (pr=pn->rlist;pr->next!=NULL;pr=pr->next);
                pr->next = (struct ranknode *)malloc(sizeof(struct ranknode));
                pr = pr->next;
                pr->rank = i;
                pr->next = NULL;
            }
        }
        
        /* send the first rank in each node list to all the processes in this node
         * and send number of threads to the first process in each node */
        tmp = 0;
        for (pn=nodelist; pn!=NULL; pn=pn->next)
        {
            cnt = 0;
            chooseid = pn->rlist->rank;
            for (pr=pn->rlist; pr!=NULL; pr=pr->next)
            {
                if (pr->rank != 0)
                    MPI_Send(&chooseid, 1, MPI_INT, pr->rank, 1, MPI_COMM_WORLD);
                cnt++;
            }
            if (chooseid != 0)
            {
                MPI_Send(&cnt, 1, MPI_INT, chooseid, 2, MPI_COMM_WORLD);
                MPI_Send(&tmp, 1, MPI_INT, chooseid, 3, MPI_COMM_WORLD);
            }
            else
            {
                threads = cnt;
                base    = 0;
            }
            tmp += cnt;
        }
    }
    
    
    /* Verify threads number */
    if (threads <= 0)
    {
        MPI_Finalize();
        errAbort("threads must be at least 1");
    }
    if (threads > 1 && (argv[3]==NULL || strcmp(argv[3],"")==0 || strcmp(argv[3], "stdin")==0))
    {
        MPI_Finalize();
        errAbort("Output name must be specified when using multi-threads");
    }
    
    

    optionInit(&argc, argv, options);
    if (argc != 4)
        usage();

    /* Get database and query sequence types and make sure they are
     * legal and compatable. */
    if (optionExists("prot"))
        qType = tType = gftProt;
    if (optionExists("t"))
        tType = gfTypeFromName(optionVal("t", NULL));
    trimA = optionExists("trimA") || optionExists("trima");
    trimT = optionExists("trimT") || optionExists("trimt");
    trimHardA = optionExists("trimHardA");
    switch (tType)
    {
    case gftProt:
    case gftDnaX:
        tIsProtLike = TRUE;
        break;
    case gftDna:
        tIsProtLike = FALSE;
        break;
    default:
        tIsProtLike = FALSE;
        MPI_Finalize();
        errAbort("Illegal value for 't' parameter");
        break;
    }
    if (optionExists("q"))
        qType = gfTypeFromName(optionVal("q", NULL));
    if (qType == gftRnaX || qType == gftRna)
        trimA = TRUE;
    if (optionExists("noTrimA"))
        trimA = FALSE;
    switch (qType)
    {
    case gftProt:
    case gftDnaX:
    case gftRnaX:
        minIdentity = 25;
        qIsProtLike = TRUE;
        break;
    default:
        qIsProtLike = FALSE;
        break;
    }
    if ((tIsProtLike ^ qIsProtLike) != 0)
    {
        MPI_Finalize();
        errAbort("t and q must both be either protein or dna");
    }

    /* Set default tile size for protein-based comparisons. */
    if (tIsProtLike)
    {
        tileSize = 5;
        minMatch = 1;
        oneOff = FALSE;
        maxGap = 0;
    }

    /* Get tile size and related parameters from user and make sure
     * they are within range. */
    tileSize = optionInt("tileSize", tileSize);
    stepSize = optionInt("stepSize", tileSize);
    minMatch = optionInt("minMatch", minMatch);
    oneOff = optionExists("oneOff");
    fastMap = optionExists("fastMap");
    minScore = optionInt("minScore", minScore);
    maxGap = optionInt("maxGap", maxGap);
    minRepDivergence = optionFloat("minRepDivergence", minRepDivergence);
    minIdentity = optionFloat("minIdentity", minIdentity);
    gfCheckTileSize(tileSize, tIsProtLike);
    if (minMatch < 0)
    {
        MPI_Finalize();
        errAbort("minMatch must be at least 1");
    }
    if (maxGap > 100)
    {
        MPI_Finalize();
        errAbort("maxGap must be less than 100");
    }


    /* Set repMatch parameter from command line, or
     * to reasonable value that depends on tile size. */
    if (optionExists("repMatch"))
        repMatch = optionInt("repMatch", repMatch);
    else
        repMatch = gfDefaultRepMatch(tileSize, stepSize, tIsProtLike);

    /* Gather last few command line options. */
    noHead = optionExists("noHead");
    ooc = optionVal("ooc", NULL);
    makeOoc = optionVal("makeOoc", NULL);
    mask = optionVal("mask", NULL);
    qMask = optionVal("qMask", NULL);
    repeats = optionVal("repeats", NULL);
    if (repeats != NULL && mask != NULL && differentString(repeats, mask))
    {
        MPI_Finalize();
        errAbort("The -mask and -repeat settings disagree.  "
                 "You can just omit -repeat if -mask is on");
    }
    if (mask != NULL)	/* Mask setting will also set repeats. */
        repeats = mask;
    outputFormat = optionVal("out", outputFormat);
    dotEvery = optionInt("dots", 0);
    /* set global for fuzzy find functions */
    setFfIntronMax(optionInt("maxIntron", ffIntronMaxDefault));
    setFfExtendThroughN(optionExists("extendThroughN"));  
    


    out=(FILE**)malloc(sizeof(FILE*) * threads);
    for (i=0; i<threads; i++)
    {
        if (base + i == 0)
            strcpy(buf, argv[3]);
        else
            sprintf(buf, "%s.tmp.%d", argv[3], base+i);
        out[i] = mustOpen(buf, "w");
    }

    
    gfClientFileArray(argv[2], &queryFiles, &queryCount);
    lf=(struct lineFile **)malloc(sizeof(struct lineFile *) * threads);
    if (id == 0)
    {
        /* get number of lines that each process/thread should process */      
        queryCount=0;
        struct lineFile *tlf = lineFileOpen(queryFiles[0], TRUE);
        while (faMixedSpeedReadNext(tlf, NULL, NULL, NULL, &faFastBuf, &faFastBufSize))
            queryCount++;
        queryCount=queryCount/numproc+1;
        
        
        /* get the offset of each file handler for each process/thread */
        lineFileRewind(tlf);
        offsets = (long long int *)malloc(sizeof(long long int) * numproc);
        offsets[0] = 0;
        for (i=1; i<numproc; i++)
        {
            cnt=queryCount;
            while (cnt-- && faMixedSpeedReadNext(tlf, NULL, NULL, NULL, &faFastBuf, &faFastBufSize));
            offsets[i] = tlf->bufOffsetInFile + tlf->lineStart;
        }
        lineFileClose(&tlf);
        faFreeFastBuf(&faFastBuf, &faFastBufSize);
        
        
        /* Distribute each file handler offset to its corresponding process/thread */
        tmp = 0;
        for (pn=nodelist; pn!=NULL; pn=pn->next)
        {
            cnt = 0;
            chooseid = pn->rlist->rank;
            for (pr=pn->rlist; pr!=NULL; pr=pr->next)
                cnt++;
            if (chooseid != 0)
            {
                MPI_Send(&queryCount, 1,   MPI_INT, chooseid, 4, MPI_COMM_WORLD);
                MPI_Send(offsets+tmp, cnt, MPI_LONG_LONG_INT, chooseid, 5, MPI_COMM_WORLD);
            }
            else
            {
                for (i=0; i<threads; i++)
                {
                    lf[i] = lineFileOpen(queryFiles[0], TRUE);
                    lineFileSeek(lf[i], offsets[i], SEEK_SET);
                }
            }
            tmp += cnt;
        }
        
        /* free applied memory */
        pn=nodelist;
        while(pn != NULL)
        {
            pr = pn->rlist;
            while(pr != NULL)
            {
                prt = pr->next;
                free(pr);
                pr = prt;
            }
            pnt = pn->next;
            free(pn);
            pn = pnt;
        }
        
        free(offsets);
    }
    else
    {
        offsets = (long long int *)malloc(sizeof(long long int) * threads);
        
        MPI_Recv(&queryCount, 1,   MPI_INT, 0, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(offsets, threads, MPI_LONG_LONG_INT, 0, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        for (i=0; i<threads; i++)
        {
            lf[i] = lineFileOpen(queryFiles[0], TRUE);
            lineFileSeek(lf[i], offsets[i], SEEK_SET);
        }
        
        free(offsets);
    }
    MPI_Finalize();
    


    /* Call routine that does the work. */
    blat(argv[1], queryCount, queryFiles, lf, out);
    
    

    for (i=0; i<threads; i++)
    {
        lineFileClose(&(lf[i]));
        carefulClose(&(out[i]));
    }
    free(lf);
    free(out);
    
    
    for (i=0; i<threads; i++)
    {
        if (id==0 && i==0) continue;
        sprintf(buf, "%s.tmp.%d", argv[3], base+i);
        sprintf(namebuf, "%s.%d", argv[3], base+i);
        rename(buf, namebuf);
    }
    
    
    if (id == 0 && numproc > 1)
    {
        fres = mustOpen(argv[3], "ab");
        for (i=1; i<numproc; i++)
        {
            sprintf(buf, "%s.%d", argv[3], i);
            
            while((ftmp = fopen(buf, "rb")) == NULL)
                sleep(10);
            
            while((cnt=fread(buf, 1, 1024*64, ftmp))>0)
            {
                tmp=fwrite(buf, 1, cnt, fres);
                if (tmp!=cnt)
                {
                    printf("Merge files failed\n");
                    return 1;
                }
            }
            fclose(ftmp);
            sprintf(buf, "%s.%d", argv[3], i);
            remove(buf);
        }
        carefulClose(&fres);

    }
    
    
    return 0;
}

/*********************************************************************************/
/*       uberpolka - the pulsar koinzidenz analysis code for einstein@home       */
/*                                                                               */
/*			               X. Siemens                                */
/*                   (takes in two Fstats file to look for coincidence)          */
/*                                                                               */
/*                                  UWM - January  2005                          */
/*********************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <lal/LALDatatypes.h>
#include <lal/LALMalloc.h>
#include <lal/LALConstants.h>
#include <lal/LALStatusMacros.h>
#include <lal/ConfigFile.h>

#include <lalapps.h>

#include "getopt.h"

RCSID ("$Id$");

/* some error codes and messages */
#define POLKAC_ENULL  		1
#define POLKAC_ENONULL 		2
#define POLKAC_ESYS 		3
#define POLKAC_EINVALIDFSTATS	4
#define POLKAC_EMEM		5

#define POLKAC_MSGENULL 	"Arguments contained an unexpected null pointer"
#define POLKAC_MSGENONULL	"Input pointer was not NULL"
#define POLKAC_MSGESYS		"System call failed (probably file IO"
#define POLKAC_MSGEINVALIDFSTATS "Invalid Fstats file"
#define POLKAC_MSGEMEM		"Sorry, ran out of memory... bye."

#ifndef USE_BOINC
#define USE_BOINC 0
#endif

#if USE_BOINC
#include "boinc_api.h"
#include "filesys.h"

#define fopen boinc_fopen
extern CHAR *Outputfilename;
#endif

struct PolkaCommandLineArgsTag 
{
  char *FstatsFile1; /* Names of Fstat files to be read in */
  char *FstatsFile2;
  char *OutputFile;
  REAL8 Deltaf;      /* Size of coincidence window in Hz */
  REAL8 DeltaAlpha;  /* Size of coincidence window in radians */
  REAL8 DeltaDelta;  /* Size of coincidence window in radians */
  REAL8 fmin;        /* Minimum frequency of candidate in first IFO */
  REAL8 fmax;        /* Maximum frequency of candidate in first IFO */
  UINT4 EAH;         /* Einstein at home flag for alternative output */ 
} PolkaCommandLineArgs;

/* This structure contains the indices corresponding to the 
coarse frequency and sky bins */
typedef struct CandINDICESTag
{
  INT4 iFreq;
  INT4 iDelta;
  INT4 iAlpha;
} CandINDICES;

typedef struct CandidateTag 
{
  UINT4 length;	   /* number of candidates in list */
  REAL8 *f;        /* Frequency */
  REAL8 *Alpha;    /* longitude */
  REAL8 *Delta;    /* latitude */
  REAL8 *F;        /* Maximum value of F for the cluster */
  REAL8 *fa;       /* false alarm probability for that candidate */
  UINT4 *Ctag;     /* tag for candidate if it's been found in coincidence */
  INT4  *CtagCounter;     /* contains the cumulative sum of coincident candidates so far */
  CandINDICES *CI;        /* indices for binning  */
} CandidateList;

typedef struct CoincidentCandidateTag 
{
  REAL8 f1;		/* Frequencies */
  REAL8 f2;
  REAL8 Alpha1;		/* longitude */
  REAL8 Alpha2;
  REAL8 Delta1;		/* latitude */
  REAL8 Delta2;
  REAL8 F1;		/* Maximum value of F for the cluster */
  REAL8 F2;
  REAL8 fa;		/* false alarm probabilities for that candidate */
  REAL8 fa1;
  REAL8 fa2;       	
} CoincidentCandidate;

typedef struct CoincidentPairsTag 
{
  UINT4 c1;             /* number in Fstats file that corresponds to first member of pair */
  UINT4 c2;             /* number in Fstats file that corresponds to second member of pair */
  REAL8 fa;             /* joint false alarm for that pair */
} CoincidentPairs;

int ReadCommandLine(int argc,char *argv[],struct PolkaCommandLineArgsTag *CLA);
int ReadCandidateFiles(struct PolkaCommandLineArgsTag CLA);
int compareC1IStructs(const void *ip, const void *jp);
int compareC2IStructs(const void *ip, const void *jp);
int compareCCfa(const void *ip, const void *jp);
int compareCPfa(const void *ip, const void *jp);
void locate(double xx[], int n, double x, int *j, int *indices);
void ReadOneCandidateFile (LALStatus *stat, CandidateList *CList, const char *fname,struct PolkaCommandLineArgsTag CLA);

extern INT4 lalDebugLevel;

CandidateList CList1, CList2; /* treat up to 4 candidate files */

CoincidentCandidate *CC;
CoincidentPairs *CP;

#ifndef FALSE
#define FALSE (1==0)
#endif
#ifndef TRUE
#define TRUE  (1==1)
#endif

/* main() mapped to polka() if using boinc */
#if USE_BOINC
int polka(int argc,char *argv[])
#else
int main(int argc,char *argv[]) 
#endif
{
  INT4 *indices1=NULL,*indices2=NULL,*indicesCCfa=NULL;
  REAL8 MaxAngularDistance;
  UINT4 i;
  UINT4 numCoincidences = 0;
  FILE *fpOut;
  lalDebugLevel = 3;
#if USE_BOINC
  static char resolved_filename[256];
#endif

  /* Reads command line arguments */
  if (ReadCommandLine(argc,argv,&PolkaCommandLineArgs)) return 1;

  /* Reads in candidare files */
  if (ReadCandidateFiles(PolkaCommandLineArgs)) return 2;

  /* create arrays of indices */
  if (!(indices1=(INT4 *)LALMalloc(sizeof(INT4) * CList1.length))){
    fprintf(stderr,"Unable to allocate index1 array in main\n");
    return 1;
  }
  if (!(indices2=(INT4 *)LALMalloc(sizeof(INT4) * CList2.length))){
    fprintf(stderr,"Unable to allocate index2 array in main\n");
    return 1;
  }

  /* populate arrays of indices */
  for (i=0;i<CList1.length;i++) indices1[i]=i;
  for (i=0;i<CList2.length;i++) indices2[i]=i;

  /* sort arrays of indices in DECREASING order*/
  qsort((void *)indices1, (size_t)CList1.length, sizeof(int), compareC1IStructs);
  qsort((void *)indices2, (size_t)CList2.length, sizeof(int), compareC2IStructs);


  LALFree(indices1);
  LALFree(indices2);

  /* freeing a CList is a bit tedious, so we use a macro */
#define freeCList(x) do { LALFree((x).f); LALFree((x).Alpha); LALFree((x).Delta); LALFree((x).F); LALFree((x).fa); LALFree((x).Ctag);LALFree((x).CtagCounter);LALFree((x).CI);} while(0)
  
  
  

  freeCList(CList1);
  freeCList(CList2);
  
  LALCheckMemoryLeaks(); 

  return 0;

}


/*******************************************************************************/

/* Sorting function to sort 1st candidate indices DECREASING order of f, delta, alpha */
int compareC1IStructs(const void *ip, const void *jp)
{
  INT4 ifreq1,ifreq2;

  ifreq1=CList1.CI[*(const int *)ip].iFreq;
  ifreq2=CList1.CI[*(const int *)jp].iFreq;

  if (ifreq1 < ifreq2)
    return 1;

  if (ifreq1 > ifreq2)
    return -1;
  
  if (ifreq1 == ifreq2)
    {
      INT4 iDelta1, iDelta2;

      iDelta1=CList1.CI[*(const int *)ip].iDelta;
      iDelta2=CList1.CI[*(const int *)jp].iDelta;
      
      if (iDelta1 < iDelta2)
	return 1;

      if (iDelta1 > iDelta2)
	return -1;
  
      if (iDelta1 == iDelta2)
	{
	  INT4 iAlpha1, iAlpha2;

	  iAlpha1=CList1.CI[*(const int *)ip].iAlpha;
	  iAlpha2=CList1.CI[*(const int *)jp].iAlpha;
      
	  if (iAlpha1 < iAlpha2)
	    return 1;

	  if (iAlpha1 > iAlpha2)
	    return -1;

	  if (iAlpha1 == iAlpha2)
	    return (ip < jp);
	}
    }
  
  return (ip < jp);

}

/*******************************************************************************/

/* Sorting function to sort 1st candidate indices DECREASING order of f, delta, alpha */
int compareC2IStructs(const void *ip, const void *jp)
{
  INT4 ifreq1,ifreq2;

  ifreq1=CList2.CI[*(const int *)ip].iFreq;
  ifreq2=CList2.CI[*(const int *)jp].iFreq;

  if (ifreq1 < ifreq2)
    return 1;

  if (ifreq1 > ifreq2)
    return -1;
  
  if (ifreq1 == ifreq2)
    {
      INT4 iDelta1, iDelta2;

      iDelta1=CList2.CI[*(const int *)ip].iDelta;
      iDelta2=CList2.CI[*(const int *)jp].iDelta;
      
      if (iDelta1 < iDelta2)
	return 1;

      if (iDelta1 > iDelta2)
	return -1;
  
      if (iDelta1 == iDelta2)
	{
	  INT4 iAlpha1, iAlpha2;

	  iAlpha1=CList2.CI[*(const int *)ip].iAlpha;
	  iAlpha2=CList2.CI[*(const int *)jp].iAlpha;
      
	  if (iAlpha1 < iAlpha2)
	    return 1;

	  if (iAlpha1 > iAlpha2)
	    return -1;

	  if (iAlpha1 == iAlpha2)
	    return (ip < jp);
	}
    }

  return (ip < jp);
}

/*******************************************************************************/

int ReadCandidateFiles(struct PolkaCommandLineArgsTag CLA)
{
  LALStatus status = blank_status;	/* initialize status */

  ReadOneCandidateFile (&status, &CList1, CLA.FstatsFile1,CLA);
  if (status.statusCode != 0) {
    REPORTSTATUS (&status);
    return 1;
  }
  ReadOneCandidateFile (&status, &CList2, CLA.FstatsFile2,CLA);
  if (status.statusCode != 0) {
    REPORTSTATUS (&status);
    return 1;
  }
  return 0;

} /* ReadCandidateFiles() */


/*******************************************************************************/

#define DONE_MARKER "%DONE"
/* read and parse the given candidate 'Fstats'-file fname into the candidate-list CList */
void 
ReadOneCandidateFile (LALStatus *stat, CandidateList *CList, const char *fname, struct PolkaCommandLineArgsTag CLA)
{
  UINT4 i;
  UINT4 numlines;
  REAL8 dmp;
  LALParsedDataFile *Fstats =NULL;	/* pre-parsed contents of Fstats-file */
  const CHAR *thisline;
  CandidateList cands;
  
  INITSTATUS( stat, "ReadOneCandidateFile", rcsid );
  ATTATCHSTATUSPTR (stat);
 
  ASSERT ( fname, stat, POLKAC_ENULL, POLKAC_MSGENULL);
  ASSERT ( CList, stat, POLKAC_ENULL, POLKAC_MSGENULL);
  ASSERT ( CList->f == NULL && CList->Alpha == NULL && CList->Delta == NULL 
	   && CList->F == NULL && CList->fa == NULL && CList->Ctag == NULL && CList->CtagCounter == NULL, 
 	   stat, POLKAC_ENONULL, POLKAC_MSGENONULL);

  /* ------ Open and read candidate file ------ */
  TRY ( LALParseDataFile (stat->statusPtr, &Fstats, fname), stat);

  numlines = Fstats->lines->nTokens; /* how many lines of data */

  if ( numlines == 0) 
    {
      LALPrintError ("ERROR: File '%s' is empty and is not properly terminated by '%s' marker!\n\n", fname, DONE_MARKER);
      TRY (LALDestroyParsedDataFile ( stat->statusPtr, &Fstats ), stat);
      ABORT (stat, POLKAC_EINVALIDFSTATS, POLKAC_MSGEINVALIDFSTATS);
    }

  /* check validity of this Fstats-file */
  thisline = Fstats->lines->tokens[numlines-1];	/* get last line */
  if ( strcmp(thisline, DONE_MARKER ) ) 
    {
      LALPrintError ("ERROR: File '%s' is not properly terminated by '%s' marker!\n\n", fname, DONE_MARKER);
      TRY (LALDestroyParsedDataFile ( stat->statusPtr, &Fstats ), stat);
      ABORT (stat, POLKAC_EINVALIDFSTATS, POLKAC_MSGEINVALIDFSTATS);
    }
  else
    numlines --; 	/* avoid stepping on DONE-marker */
  
  /* reserve memory for fstats-file contents */
  cands.f     = LALCalloc (numlines, sizeof(REAL8));
  cands.Alpha = LALCalloc (numlines, sizeof(REAL8));
  cands.Delta = LALCalloc (numlines, sizeof(REAL8));
  cands.F     = LALCalloc (numlines, sizeof(REAL8));
  cands.fa    = LALCalloc (numlines, sizeof(REAL8));
  cands.Ctag  = LALCalloc (numlines, sizeof(UINT4));
  cands.CtagCounter  = LALCalloc (numlines, sizeof(INT4));
  cands.CI  = LALCalloc (numlines, sizeof(CandINDICES));

  if ( !cands.f || !cands.Alpha || !cands.Delta || !cands.F || !cands.fa || !cands.Ctag || !cands.CtagCounter || !cands.CI )
    {
      TRY( LALDestroyParsedDataFile ( stat->statusPtr, &Fstats ), stat);
      ABORT (stat, POLKAC_EMEM, POLKAC_MSGEMEM);
    }

  for (i=0; i < numlines; i++)
    {
      int read;
      
      cands.Ctag[i]=0;
      cands.CtagCounter[i]=-1;

      thisline = Fstats->lines->tokens[i];
      read = sscanf (thisline, 
		     "%" LAL_REAL8_FORMAT " %" LAL_REAL8_FORMAT " %" LAL_REAL8_FORMAT " %" LAL_REAL8_FORMAT 
		     " %" LAL_REAL8_FORMAT " %" LAL_REAL8_FORMAT " %" LAL_REAL8_FORMAT, 
		     &(cands.f[i]), &(cands.Alpha[i]), &(cands.Delta[i]), &dmp, &dmp, &dmp, &(cands.F[i]) );

      cands.CI[i].iFreq=(INT4) (cands.f[i]/(2*CLA.Deltaf));
      cands.CI[i].iDelta=(INT4) (cands.Delta[i]/(2*CLA.DeltaDelta));
      cands.CI[i].iAlpha=0;


      if ( read != 7 )
	{
	  LALPrintError ("Failed to parse line %d in file '%s' \n", i+1, fname);
	  TRY (LALDestroyParsedDataFile ( stat->statusPtr, &Fstats ), stat);
	  LALFree (cands.f);
	  LALFree (cands.Alpha);
	  LALFree (cands.Delta);
	  LALFree (cands.F);
	  LALFree (cands.fa);
	  LALFree (cands.Ctag);
	  LALFree (cands.CtagCounter);
	  ABORT (stat, POLKAC_EINVALIDFSTATS, POLKAC_MSGEINVALIDFSTATS);
	}
    } /* for i < numlines */
 
  /* we're done: get rid of raw data-file */
  TRY ( LALDestroyParsedDataFile ( stat->statusPtr, &Fstats ), stat);
  
  /* return final candidate-list */
  CList->length = numlines;
  CList->f      = cands.f;
  CList->Alpha  = cands.Alpha;
  CList->Delta  = cands.Delta;
  CList->F      = cands.F;
  CList->fa     = cands.fa;
  CList->Ctag   = cands.Ctag;
  CList->CtagCounter   = cands.CtagCounter;
  CList->CI   = cands.CI;

  DETATCHSTATUSPTR (stat);
  RETURN (stat);

} /* ReadOneCandidateFile() */

/*******************************************************************************/


int ReadCommandLine(int argc,char *argv[],struct PolkaCommandLineArgsTag *CLA) 
{
  INT2 errflg = 0;
  INT4 c; 
  INT4 option_index = 0;

  const char *optstring = "h1:2:f:a:d:m:M:o:s:e:b";
  struct option long_options[] =
    {
      {"fstatsfile1", 		required_argument, 0, 	'1'},
      {"fstatsfile2", 		required_argument, 0, 	'2'},
      {"frequency-window", 	required_argument, 0, 	'f'},
      {"delta-window", 		required_argument, 0, 	'd'},
      {"alpha-window", 		required_argument, 0, 	'a'},
      {"fmin",   		required_argument, 0, 	's'},
      {"fmax",   		required_argument, 0, 	'e'},
      {"outputfile", 		required_argument, 0, 	'o'},
      {"EAHoutput", 		no_argument, 0, 	'b'},
      {"help", 			no_argument, 0, 	'h'},
      {0, 0, 0, 0}
    };

  /* Initialize default values */
  CLA->FstatsFile1=NULL;
  CLA->FstatsFile2=NULL;
  CLA->OutputFile=NULL;
  CLA->Deltaf=0.0;
  CLA->DeltaAlpha=0;
  CLA->DeltaDelta=0;
  CLA->fmin=0;
  CLA->fmax=0;
  CLA->EAH=0;

  /* reset gnu getopt */
  optind = 0;

  /* Scan through list of command line arguments */
  while (1)
    {
      c = getopt_long(argc, argv, optstring, long_options, &option_index);      
      if (c == -1) 
	break;
      switch (c) {
      case '1':
	/* SFT directory */
	CLA->FstatsFile1=optarg;
	break;
      case '2':
	/* calibration files directory */
	CLA->FstatsFile2=optarg;
	break;
      case 'o':
	/* calibration files directory */
	CLA->OutputFile=optarg;
	break;
      case 'f':
	/* Spin down order */
	CLA->Deltaf=atof(optarg);
	break;
      case 'a':
	/* Spin down order */
	CLA->DeltaAlpha=atof(optarg);
	break;
      case 's':
	/* Spin down order */
	CLA->fmin=atof(optarg);
	break;
      case 'e':
	/* Spin down order */
	CLA->fmax=atof(optarg);
	break;
      case 'd':
	/* Spin down order */
	CLA->DeltaDelta=atof(optarg);
	break;
      case 'b':
	/* Spin down order */
	CLA->EAH=1;
	break;
      case 'h':
	/* print usage/help message */
	fprintf(stderr,"Arguments are (defaults):\n");
	fprintf(stderr,"\t--fstatsfile1 (-1)\tSTRING\tFirst candidates Fstats file\n");
	fprintf(stderr,"\t--fstatsfile2 (-2)\tSTRING\tSecond candidates Fstats file\n");
	fprintf(stderr,"\t--outputfile  (-o)\tSTRING\tName of ouput candidates file\n");
	fprintf(stderr,"\t--frequency-window (-f)\tFLOAT\tFrequency window in Hz (0.0)\n");
	fprintf(stderr,"\t--alpha-window (-a)\tFLOAT\tAlpha window in radians (0.0)\n");
	fprintf(stderr,"\t--delta-window (-d)\tFLOAT\tDelta window in radians (0.0)\n");
	fprintf(stderr,"\t--fmin (-s)\tFLOAT\t Minimum frequency of candidate in 1st IFO\n");
	fprintf(stderr,"\t--fmax (-e)\tFLOAT\t Maximum frequency of candidate in 1st IFO\n");
	fprintf(stderr,"\t--EAHoutput (-b)\tFLAG\t Einstein at home output flag. \n");
	fprintf(stderr,"\t--help        (-h)\t\tThis message\n");
	exit(0);
	break;
      default:
	/* unrecognized option */
	errflg++;
	fprintf(stderr,"Unrecognized option argument %c\n",c);
	exit(1);
	break;
      }
    }

  if(CLA->FstatsFile1 == NULL)
    {
      fprintf(stderr,"No 1st candidates file specified; input with -1 option.\n");
      fprintf(stderr,"For help type ./polka -h \n");
      return 1;
    }      
  if(CLA->FstatsFile2 == NULL)
    {
      fprintf(stderr,"No 2nd candidates file specified; input with -2 option.\n");
      fprintf(stderr,"For help type ./polka -h \n");
      return 1;
    }      
  if(CLA->OutputFile == NULL)
    {
      fprintf(stderr,"No ouput filename specified; input with -o option.\n");
      fprintf(stderr,"For help type ./polka -h \n");
      return 1;
    }      

  if(CLA->fmin == 0.0)
    {
      fprintf(stderr,"No minimum frequency specified.\n");
      fprintf(stderr,"For help type ./polka -h \n");
      return 1;
    }      

  if(CLA->fmax == 0.0)
    {
      fprintf(stderr,"No maximum frequency specified.\n");
      fprintf(stderr,"For help type ./polka -h \n");
      return 1;
    }      

  return errflg;
}


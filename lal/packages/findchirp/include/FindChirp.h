/*----------------------------------------------------------------------- 
 * 
 * File Name: FindChirp.h
 *
 * Author: Allen, B., Brown, D. A. and Creighton, J. D. E.
 * 
 * Revision: $Id$
 * 
 *-----------------------------------------------------------------------
 */

#if 0
<lalVerbatim file="FindChirpHV">
Author: Allen, B., Brown, D. A. and Creighton, J. D. E.
$Id$
</lalVerbatim> 

<lalLaTeX>

\section{Header \texttt{FindChirp.h}}
\label{s:FindChirp.h}

Provides routines to filter IFO data for binary inspiral chirps.

</lalLaTeX>
#endif

#ifndef _FINDCHIRPH_H
#define _FINDCHIRPH_H

#include <lal/LALDatatypes.h>
#include <lal/ComplexFFT.h>
#include <lal/DataBuffer.h>
#ifdef LAL_ENABLE_MPI
#include <lal/Comm.h>
#endif
#include <lal/LALInspiral.h>
#include <lal/FindChirpChisq.h>

#ifdef  __cplusplus
extern "C" {
#endif


NRCSID (FINDCHIRPH, "$Id$");

#if 0
<lalLaTeX> 
\subsection*{Error codes} 
</lalLaTeX>
#endif
/* <lalErrTable> */
#define FINDCHIRPH_ENULL 1
#define FINDCHIRPH_ENNUL 2
#define FINDCHIRPH_EALOC 3
#define FINDCHIRPH_ENUMZ 5
#define FINDCHIRPH_ESEGZ 6
#define FINDCHIRPH_ECHIZ 7
#define FINDCHIRPH_EDTZO 8
#define FINDCHIRPH_ETRNC 10
#define FINDCHIRPH_EFLOW 11
#define FINDCHIRPH_EFREE 12
#define FINDCHIRPH_ERHOT 15
#define FINDCHIRPH_ECHIT 16
#define FINDCHIRPH_ECRUP 17
#define FINDCHIRPH_MSGENULL "Null pointer"
#define FINDCHIRPH_MSGENNUL "Non-null pointer"
#define FINDCHIRPH_MSGEALOC "Memory allocation error"
#define FINDCHIRPH_MSGENUMZ "Invalid number of points in segment"
#define FINDCHIRPH_MSGESEGZ "Invalid number of segments"
#define FINDCHIRPH_MSGECHIZ "Invalid number of chi squared bins"
#define FINDCHIRPH_MSGEDTZO "deltaT is zero or negative"
#define FINDCHIRPH_MSGETRNC "Duration of inverse spectrum in time domain is negative"
#define FINDCHIRPH_MSGEFLOW "Inverse spectrum low frequency cutoff is negative"
#define FINDCHIRPH_MSGEFREE "Memory free error"
#define FINDCHIRPH_MSGERHOT "Rhosq threshold is zero or negative"
#define FINDCHIRPH_MSGECHIT "Chisq threshold is zero or negative"
#define FINDCHIRPH_MSGECRUP "Attempting to filter corrupted data"
/* </lalErrTable> */


/*
 *
 * typedefs of structures used by the findchip functions
 *
 */


/* structure for describing a binary insipral event */
typedef struct
tagInspiralEvent
{
  UINT4                         id;
  UINT4                         segmentNumber;
  LIGOTimeGPS                   time;
  UINT4                         timeIndex;
  InspiralTemplate              tmplt;
  REAL4                         snrsq;
  REAL4                         chisq;
  REAL4                         sigma;
  REAL4                         effDist;
  struct tagInspiralEvent      *next;
}
InspiralEvent;

/* vector of DataSegment, as defined the framedata package */
typedef struct
tagDataSegmentVector
{
  UINT4                         length;
  DataSegment                  *data;
}
DataSegmentVector;

/* processed data segment used by FindChirp filter routine */
typedef struct
tagFindChirpSegment
{
  COMPLEX8FrequencySeries      *data;
  UINT4Vector                  *chisqBinVec;
  REAL8                         deltaT;
  REAL4                         segNorm;
  REAL4                         fLow;
  UINT4                         invSpecTrunc;
  UINT4                         number;
}
FindChirpSegment;

/* vector of FindChirpSegment defined above */
typedef struct
tagFindChirpSegmentVector
{
  UINT4                         length;
  FindChirpSegment             *data;
}
FindChirpSegmentVector;

/* structure to contain an inspiral template */
typedef struct
tagFindChirpTemplate
{
  COMPLEX8Vector               *data;
  REAL4                         tmpltNorm;
}
FindChirpTemplate;


/*
 *
 * typedefs of parameter structures used by functions in findchirp
 *
 */


/* parameter structure for all init funtions */
typedef struct
tagFindChirpInitParams
{
  UINT4                         numSegments;
  UINT4                         numPoints;
  UINT4                         numChisqBins;
  BOOLEAN                       createRhosqVec;
}
FindChirpInitParams;

/* parameter structure for the filtering function */
typedef struct
tagFindChirpFilterParams
{
  REAL4                         deltaT;
  REAL4                         rhosqThresh;
  REAL4                         chisqThresh;
  REAL4                         norm;
  BOOLEAN                       computeNegFreq;
  COMPLEX8Vector               *qVec;
  COMPLEX8Vector               *qtildeVec;
  ComplexFFTPlan               *invPlan;
  REAL4Vector                  *rhosqVec;
  REAL4Vector                  *chisqVec;
  FindChirpChisqParams         *chisqParams;
  FindChirpChisqInput          *chisqInput;
}
FindChirpFilterParams;


/*
 *
 * typedefs of input structures used by functions in findchirp
 *
 */


/* input to the filtering functions */
typedef struct
tagFindChirpFilterInput
{
  InspiralTemplate             *tmplt;
  FindChirpTemplate            *fcTmplt;
  FindChirpSegment             *segment;
}
FindChirpFilterInput;


/*
 *
 * function prototypes for memory management functions
 *
 */


void
LALCreateDataSegmentVector (
    LALStatus                  *status,
    DataSegmentVector         **vector,
    FindChirpInitParams        *params
    );

void
LALDestroyDataSegmentVector (
    LALStatus                  *status,
    DataSegmentVector         **vector
    );

void
LALCreateFindChirpSegmentVector (
    LALStatus                  *status,
    FindChirpSegmentVector    **vector,
    FindChirpInitParams        *params
    );

void
LALDestroyFindChirpSegmentVector (
    LALStatus                  *status,
    FindChirpSegmentVector    **vector
    );


/*
 *
 * function prototypes for initialization and finalization functions
 *
 */


void
LALFindChirpFilterInit (
    LALStatus                  *status,
    FindChirpFilterParams     **output,
    FindChirpInitParams        *params
    );

void
LALFindChirpFilterFinalize (
    LALStatus                  *status,
    FindChirpFilterParams     **output
    );

void
LALCreateFindChirpInput (
    LALStatus                  *status,
    FindChirpFilterInput      **output,
    FindChirpInitParams        *params
    );

void
LALDestroyFindChirpInput (
    LALStatus                  *status,
    FindChirpFilterInput      **output
    );


/*
 *
 * function prototype for the filtering function
 *
 */


void
LALFindChirpFilterSegment (
    LALStatus                  *status,
    InspiralEvent             **eventList,
    FindChirpFilterInput       *input,
    FindChirpFilterParams      *params
    );


#if 0
<lalLaTeX>
\vfill{\footnotesize\input{FindChirpHV}}
</lalLaTeX> 
#endif

#ifdef  __cplusplus
}
#endif

#endif /* _FINDCHIRPH_H */

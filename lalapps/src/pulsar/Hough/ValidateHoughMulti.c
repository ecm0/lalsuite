/*
 *  Copyright (C) 2005 Badri Krishnan, Alicia Sintes  
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the 
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *  MA  02111-1307  USA
 */

/**
 * \file
 * \ingroup lalapps_pulsar_Hough
 * \author Badri Krishnan, Alicia Sintes
 * \brief Driver code for performing Hough transform search on non-demodulated
 * data using SFTs from possible multiple IFOs
 *
 * History:   Created by Sintes and Krishnan July 15, 2006
 */


#include "./DriveHoughColor.h"
#include "./MCInjectHoughMulti.h"

/* globals, constants and defaults */



/* boolean global variables for controlling output */
BOOLEAN uvar_printEvents, uvar_printTemplates, uvar_printMaps, uvar_printStats, uvar_printSigma;

/* #define EARTHEPHEMERIS "./earth05-09.dat" */
/* #define SUNEPHEMERIS "./sun05-09.dat"    */

#define EARTHEPHEMERIS "/home/badkri/lscsoft/share/lal/earth05-09.dat"
#define SUNEPHEMERIS "/home/badkri/lscsoft/share/lal/sun05-09.dat"

#define MAXFILENAMELENGTH 512 /* maximum # of characters  of a filename */

#define DIROUT "./outMulti"   /* output directory */
#define BASENAMEOUT "HM"    /* prefix file output */

#define THRESHOLD 1.6 /* thresold for peak selection, with respect to the
                              the averaged power in the search band */
#define FALSEALARM 1.0e-9 /* Hough false alarm for candidate selection */
#define SKYFILE "./skypatchfile"      
#define F0 310.0   /*  frequency to build the LUT and start search */
#define FBAND 0.05   /* search frequency band  */
#define NFSIZE  21   /* n-freq. span of the cylinder, to account for spin-down search */
#define BLOCKSRNGMED 101 /* Running median window size */

#define TRUE (1==1)
#define FALSE (1==0)

/* local function prototype */


/******************************************/

int main(int argc, char *argv[]){

  /* LALStatus pointer */
  static LALStatus  status;  
  
  /* time and velocity  */
  static LIGOTimeGPSVector    timeV;
  static REAL8Cart3CoorVector velV;
  static REAL8Vector          timeDiffV;
  LIGOTimeGPS firstTimeStamp;

  /* standard pulsar sft types */ 
  MultiSFTVector *inputSFTs = NULL;
  UINT4 binsSFT;
  UINT4 sftFminBin;
  UINT4 numsft;

  INT4 k;

  /* information about all the ifos */
  MultiDetectorStateSeries *mdetStates = NULL;
  UINT4 numifo;

  /* vector of weights */
  REAL8Vector weightsV;

  /* ephemeris */
  EphemerisData    *edat=NULL;

  static UCHARPeakGram       pg1;
  static HoughTemplate  pulsarTemplate;
  static REAL8Vector  foft; 

  /* miscellaneous */
  UINT4  mObsCoh;
  REAL8  timeBase, deltaF;
  REAL8  numberCount;

  /* sft constraint variables */
  LIGOTimeGPS startTimeGPS, endTimeGPS;
  LIGOTimeGPSVector *inputTimeStampsVector=NULL;


  REAL8  alphaPeak, meanN, sigmaN;

  /* user input variables */
  BOOLEAN  uvar_weighAM, uvar_weighNoise;
  INT4     uvar_blocksRngMed, uvar_nfSizeCylinder, uvar_maxBinsClean;
  REAL8    uvar_startTime, uvar_endTime;
  REAL8    uvar_fStart, uvar_peakThreshold, uvar_fSearchBand;
  REAL8    uvar_Alpha, uvar_Delta, uvar_Freq, uvar_fdot;
  REAL8    uvar_AlphaWeight, uvar_DeltaWeight;
  CHAR     *uvar_earthEphemeris=NULL;
  CHAR     *uvar_sunEphemeris=NULL;
  CHAR     *uvar_sftDir=NULL;
  CHAR     *uvar_timeStampsFile=NULL;
  CHAR     *uvar_outfile=NULL;
  LALStringVector *uvar_linefiles=NULL;


  /* Set up the default parameters */  

  /* LAL error-handler */
  lal_errhandler = LAL_ERR_EXIT;
    
  uvar_weighAM = TRUE;
  uvar_weighNoise = TRUE;
  uvar_blocksRngMed = BLOCKSRNGMED;
  uvar_nfSizeCylinder = NFSIZE;
  uvar_fStart = F0;
  uvar_fSearchBand = FBAND;
  uvar_peakThreshold = THRESHOLD;
  uvar_maxBinsClean = 100;
  uvar_startTime= 0;
  uvar_endTime = LAL_INT4_MAX;

  uvar_Alpha = 1.0;
  uvar_Delta = 1.0;
  uvar_Freq = 310.0;
  uvar_fdot = 0.0;

  uvar_AlphaWeight = uvar_Alpha;
  uvar_DeltaWeight = uvar_Delta;

  uvar_outfile = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_outfile, "./tempout");

  uvar_earthEphemeris = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_earthEphemeris,EARTHEPHEMERIS);

  uvar_sunEphemeris = (CHAR *)LALCalloc( MAXFILENAMELENGTH , sizeof(CHAR));
  strcpy(uvar_sunEphemeris,SUNEPHEMERIS);


  /* register user input variables */
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_fStart,         "fStart",         REAL8,        'f', OPTIONAL, "Start search frequency") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_fSearchBand,    "fSearchBand",    REAL8,        'b', OPTIONAL, "Search frequency band") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_startTime,      "startTime",      REAL8,        0,   OPTIONAL, "GPS start time of observation") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_endTime,        "endTime",        REAL8,        0,   OPTIONAL, "GPS end time of observation") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_timeStampsFile, "timeStampsFile", STRING,       0,   OPTIONAL, "Input time-stamps file") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_peakThreshold,  "peakThreshold",  REAL8,        0,   OPTIONAL, "Peak selection threshold") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_weighAM,        "weighAM",        BOOLEAN,      0,   OPTIONAL, "Use amplitude modulation weights") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_weighNoise,     "weighNoise",     BOOLEAN,      0,   OPTIONAL, "Use SFT noise weights") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_earthEphemeris, "earthEphemeris", STRING,       'E', OPTIONAL, "Earth Ephemeris file") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_sunEphemeris,   "sunEphemeris",   STRING,       'S', OPTIONAL, "Sun Ephemeris file") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_sftDir,         "sftDir",         STRING,       'D', REQUIRED, "SFT filename pattern") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_linefiles,      "linefiles",      STRINGVector, 0,   OPTIONAL, "Comma separated List of linefiles (filenames must contain IFO name)") == XLAL_SUCCESS, XLAL_EFUNC);

  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_Alpha,          "Alpha",          REAL8,        0,   OPTIONAL, "Sky location (longitude)") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_Delta,          "Delta",          REAL8,        0,   OPTIONAL, "Sky location (latitude)") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_Freq,           "Freq",           REAL8,        0,   OPTIONAL, "Template frequency") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_fdot,           "fdot",           REAL8,        0,   OPTIONAL, "First spindown") == XLAL_SUCCESS, XLAL_EFUNC);

  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_AlphaWeight,    "AlphaWeight",    REAL8,        0,   OPTIONAL, "sky Alpha for weight calculation") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_DeltaWeight,    "DeltaWeight",    REAL8,        0,   OPTIONAL, "sky Delta for weight calculation") == XLAL_SUCCESS, XLAL_EFUNC);

  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_nfSizeCylinder, "nfSizeCylinder", INT4,         0,   OPTIONAL, "Size of cylinder of PHMDs") == XLAL_SUCCESS, XLAL_EFUNC);

  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_blocksRngMed,   "blocksRngMed",   INT4,         0,   OPTIONAL, "Running Median block size") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_maxBinsClean,   "maxBinsClean",   INT4,         0,   OPTIONAL, "Maximum number of bins in cleaning") == XLAL_SUCCESS, XLAL_EFUNC);
  XLAL_CHECK_MAIN( XLALRegisterNamedUvar( &uvar_outfile,        "outfile",        STRING,       0,   OPTIONAL, "output file name") == XLAL_SUCCESS, XLAL_EFUNC);


  /* read all command line variables */
  BOOLEAN should_exit = 0;
  XLAL_CHECK_MAIN( XLALUserVarReadAllInput(&should_exit, argc, argv) == XLAL_SUCCESS, XLAL_EFUNC);
  if (should_exit)
    exit(1);

  /* very basic consistency checks on user input */
  if ( uvar_fStart < 0 ) {
    fprintf(stderr, "start frequency must be positive\n");
    exit(1);
  }

  if ( uvar_fSearchBand < 0 ) {
    fprintf(stderr, "search frequency band must be positive\n");
    exit(1);
  }
 
  if ( uvar_peakThreshold < 0 ) {
    fprintf(stderr, "peak selection threshold must be positive\n");
    exit(1);
  }


  /***** start main calculations *****/



  /* read sft Files and set up weights */
  {
    /* new SFT I/O data types */
    SFTCatalog *catalog = NULL;
    static SFTConstraints constraints;

    REAL8 doppWings, f_min, f_max;

    /* set detector constraint */
    constraints.detector = NULL;

    if ( XLALUserVarWasSet( &uvar_startTime ) ) {
      XLALGPSSetREAL8(&startTimeGPS, uvar_startTime);
      constraints.minStartTime = &startTimeGPS;
    }

    if ( XLALUserVarWasSet( &uvar_endTime ) ) {
      XLALGPSSetREAL8(&endTimeGPS, uvar_endTime);
      constraints.maxStartTime = &endTimeGPS;
    }

    if ( XLALUserVarWasSet( &uvar_timeStampsFile ) ) {
      XLAL_CHECK_MAIN ( ( inputTimeStampsVector = XLALReadTimestampsFile ( uvar_timeStampsFile) ) != NULL, XLAL_EFUNC);
      constraints.timestamps = inputTimeStampsVector;
    }
    
    /* get sft catalog */
    XLAL_CHECK_MAIN( ( catalog = XLALSFTdataFind( uvar_sftDir, &constraints) ) != NULL, XLAL_EFUNC);
    if ( (catalog == NULL) || (catalog->length == 0) ) {
      fprintf (stderr,"Unable to match any SFTs with pattern '%s'\n", uvar_sftDir );
      exit(1);
    }

    /* now we can free the inputTimeStampsVector */
    if ( XLALUserVarWasSet( &uvar_timeStampsFile ) ) {
      XLALDestroyTimestampVector (inputTimeStampsVector);
    }

    /* get some sft parameters */
    mObsCoh = catalog->length; /* number of sfts */
    deltaF = catalog->data->header.deltaF;  /* frequency resolution */
    timeBase= 1.0/deltaF; /* coherent integration time */
    // unused: INT8 f0Bin = floor( uvar_fStart * timeBase + 0.5); /* initial search frequency */
    // unused: INT4 length =  uvar_fSearchBand * timeBase; /* total number of search bins - 1 */
    
    /* catalog is ordered in time so we can get start, end time and tObs*/
    firstTimeStamp = catalog->data[0].header.epoch;
    // unused: LIGOTimeGPS lastTimeStamp = catalog->data[mObsCoh - 1].header.epoch;

    /* allocate memory for velocity vector */
    velV.length = mObsCoh;
    velV.data = NULL;
    velV.data = (REAL8Cart3Coor *)LALCalloc(mObsCoh, sizeof(REAL8Cart3Coor));

    /* allocate memory for timestamps vector */
    timeV.length = mObsCoh;
    timeV.data = NULL;
    timeV.data = (LIGOTimeGPS *)LALCalloc( mObsCoh, sizeof(LIGOTimeGPS));

    /* allocate memory for vector of time differences from start */
    timeDiffV.length = mObsCoh;
    timeDiffV.data = NULL; 
    timeDiffV.data = (REAL8 *)LALCalloc(mObsCoh, sizeof(REAL8));
  
    /* add wings for Doppler modulation and running median block size*/
    doppWings = (uvar_fStart + uvar_fSearchBand) * VTOT;    
    f_min = uvar_fStart - doppWings - (uvar_blocksRngMed + uvar_nfSizeCylinder) * deltaF;
    f_max = uvar_fStart + uvar_fSearchBand + doppWings + (uvar_blocksRngMed + uvar_nfSizeCylinder) * deltaF;

    /* read sft files making sure to add extra bins for running median */
    /* read the sfts */
    XLAL_CHECK_MAIN( ( inputSFTs = XLALLoadMultiSFTs ( catalog, f_min, f_max) ) != NULL, XLAL_EFUNC);


    /* clean sfts if required */
    if ( XLALUserVarWasSet( &uvar_linefiles ) )
      {
	RandomParams *randPar=NULL;
	FILE *fpRand=NULL;
	INT4 seed, ranCount;  

	if ( (fpRand = fopen("/dev/urandom", "r")) == NULL ) {
	  fprintf(stderr,"Error in opening /dev/urandom" ); 
	  exit(1);
	} 

	if ( (ranCount = fread(&seed, sizeof(seed), 1, fpRand)) != 1 ) {
	  fprintf(stderr,"Error in getting random seed" );
	  exit(1);
	}

	LAL_CALL ( LALCreateRandomParams (&status, &randPar, seed), &status );

	LAL_CALL( LALRemoveKnownLinesInMultiSFTVector ( &status, inputSFTs, uvar_maxBinsClean, uvar_blocksRngMed, uvar_linefiles, randPar), &status);

	LAL_CALL ( LALDestroyRandomParams (&status, &randPar), &status);
	fclose(fpRand);
      } /* end cleaning */


    /* SFT info -- assume all SFTs have same length */
    numifo = inputSFTs->length;
    binsSFT = inputSFTs->data[0]->data->data->length;
    sftFminBin = (INT4) floor(inputSFTs->data[0]->data[0].f0 * timeBase + 0.5);    


    XLALDestroySFTCatalog(catalog );  	

  } /* end of sft reading block */



  /* get detector velocities weights vector, and timestamps */
  { 
    MultiNoiseWeights *multweight = NULL;    
    MultiPSDVector *multPSD = NULL;  
    UINT4 iIFO, iSFT, j;

    /*  get ephemeris  */
    XLAL_CHECK_MAIN( ( edat = XLALInitBarycenter( uvar_earthEphemeris, uvar_sunEphemeris ) ) != NULL, XLAL_EFUNC);


    /* normalize sfts */
    LAL_CALL( LALNormalizeMultiSFTVect (&status, &multPSD, inputSFTs, uvar_blocksRngMed), &status);

    /* set up weights */
    weightsV.length = mObsCoh;
    weightsV.data = (REAL8 *)LALCalloc(1, mObsCoh * sizeof(REAL8));

    /* initialize all weights to unity */
    LAL_CALL( LALHOUGHInitializeWeights( &status, &weightsV), &status);
   
    /* compute multi noise weights if required */
    if ( uvar_weighNoise ) {
      XLAL_CHECK_MAIN ( ( multweight = XLALComputeMultiNoiseWeights ( multPSD, uvar_blocksRngMed, 0) ) != NULL, XLAL_EFUNC);
    }
    
    /* we are now done with the psd */
    XLALDestroyMultiPSDVector  ( multPSD);

    /* get information about all detectors including velocity and timestamps */
    /* note that this function returns the velocity at the 
       mid-time of the SFTs -- should not make any difference */
    LAL_CALL ( LALGetMultiDetectorStates ( &status, &mdetStates, inputSFTs, edat), &status);


    /* copy the timestamps, weights, and velocity vector */
    for (j = 0, iIFO = 0; iIFO < numifo; iIFO++ ) {

      numsft = mdetStates->data[iIFO]->length;
      
      for ( iSFT = 0; iSFT < numsft; iSFT++, j++) {

	velV.data[j].x = mdetStates->data[iIFO]->data[iSFT].vDetector[0];
	velV.data[j].y = mdetStates->data[iIFO]->data[iSFT].vDetector[1];
	velV.data[j].z = mdetStates->data[iIFO]->data[iSFT].vDetector[2];

	if ( uvar_weighNoise )
	  weightsV.data[j] = multweight->data[iIFO]->data[iSFT];

	/* mid time of sfts */
	timeV.data[j] = mdetStates->data[iIFO]->data[iSFT].tGPS;

      } /* loop over SFTs */

    } /* loop over IFOs */

    if ( uvar_weighNoise ) {
      LAL_CALL( LALHOUGHNormalizeWeights( &status, &weightsV), &status);
    }

    /* compute the time difference relative to startTime for all SFTs */
    for(j = 0; j < mObsCoh; j++)
      timeDiffV.data[j] = XLALGPSDiff( timeV.data + j, &firstTimeStamp );

    if ( uvar_weighNoise ) {    
      XLALDestroyMultiNoiseWeights (multweight);
    }

  } /* end block for noise weights, velocity and time */
  

  
  /* calculate amplitude modulation weights if required */
  if (uvar_weighAM) 
    {
      MultiAMCoeffs *multiAMcoef = NULL;
      UINT4 iIFO, iSFT;
      SkyPosition skypos;      

      /* get the amplitude modulation coefficients */
      skypos.longitude = uvar_AlphaWeight;
      skypos.latitude = uvar_DeltaWeight;
      skypos.system = COORDINATESYSTEM_EQUATORIAL;
      LAL_CALL ( LALGetMultiAMCoeffs ( &status, &multiAMcoef, mdetStates, skypos), &status);
      
      /* loop over the weights and multiply them by the appropriate
	 AM coefficients */
      for ( k = 0, iIFO = 0; iIFO < numifo; iIFO++) {
	
	numsft = mdetStates->data[iIFO]->length;
	
	for ( iSFT = 0; iSFT < numsft; iSFT++, k++) {	  
	  
	  REAL8 a, b;
	  
	  a = multiAMcoef->data[iIFO]->a->data[iSFT];
	  b = multiAMcoef->data[iIFO]->b->data[iSFT];    
	  weightsV.data[k] *= (a*a + b*b);
	} /* loop over SFTs */
      } /* loop over IFOs */
      
      LAL_CALL( LALHOUGHNormalizeWeights( &status, &weightsV), &status);
      
      XLALDestroyMultiAMCoeffs ( multiAMcoef );
    } /* end AM weights calculation */


  /* misc. memory allocations */

  /* memory for one spindown */  
  pulsarTemplate.spindown.length = 1; 
  pulsarTemplate.spindown.data = NULL; 
  pulsarTemplate.spindown.data = (REAL8 *)LALMalloc(sizeof(REAL8));

  /* copy template parameters */
  pulsarTemplate.spindown.data[0] = uvar_fdot;
  pulsarTemplate.f0 = uvar_Freq;
  pulsarTemplate.latitude = uvar_Delta;
  pulsarTemplate.longitude = uvar_Alpha;

  /* memory for f(t) vector */
  foft.length = mObsCoh;
  foft.data = NULL;
  foft.data = (REAL8 *)LALMalloc(mObsCoh*sizeof(REAL8));

  /* memory for peakgram */
  pg1.length = binsSFT;
  pg1.data = NULL;
  pg1.data = (UCHAR *)LALCalloc( binsSFT, sizeof(UCHAR));
  

  /* block for calculating peakgram and number count */  
  {
    UINT4 iIFO, iSFT;
    INT4 ind;
    REAL8 sumWeightSquare;
    SFTtype  *sft;        

    /* compute mean and sigma for noise only */    
    /* first calculate the sum of the weights squared */
    sumWeightSquare = 0.0;
    for ( k = 0; k < (INT4)mObsCoh; k++)
      sumWeightSquare += weightsV.data[k] * weightsV.data[k];
    
    /* probability of selecting a peak expected mean and standard deviation for noise only */
    alphaPeak = exp( - uvar_peakThreshold);
    meanN = mObsCoh* alphaPeak; 
    sigmaN = sqrt(sumWeightSquare * alphaPeak * (1.0 - alphaPeak));
        
 
   /* the received frequency as a function of time  */
   LAL_CALL( ComputeFoft(&status, &foft, &pulsarTemplate, &timeDiffV, &velV, timeBase), &status);   
      
   numberCount = 0;

   /* loop over SFT, generate peakgram and get number count */
   UINT4 j;
   for ( j = 0, iIFO = 0; iIFO < numifo; iIFO++){
     numsft = mdetStates->data[iIFO]->length;
     
     for ( iSFT = 0; iSFT < numsft; iSFT++, j++) {
       
       sft = inputSFTs->data[iIFO]->data + iSFT;

       LAL_CALL (SFTtoUCHARPeakGram( &status, &pg1, sft, uvar_peakThreshold), &status);	    

       ind = floor( foft.data[j]*timeBase - sftFminBin + 0.5); 

       numberCount += pg1.data[ind]*weightsV.data[j];
       
     } /* loop over SFTs */	  
   } /* loop over IFOs */
   
  }
   

  {
    FILE *fp=NULL;
    fp = fopen("./tempout", "w");
    fprintf(fp, "%g  %g  %g\n", (numberCount - meanN)/sigmaN, meanN ,sigmaN);
    fprintf(stdout, "%g  %g  %g\n", (numberCount - meanN)/sigmaN, meanN ,sigmaN);
    fclose(fp);
  }


  /* free memory */
  LALFree(pulsarTemplate.spindown.data);  
  LALFree(timeV.data);
  LALFree(timeDiffV.data);
  LALFree(foft.data);
  LALFree(velV.data);

  LALFree(weightsV.data);

  XLALDestroyMultiDetectorStateSeries ( mdetStates );

  XLALDestroyEphemerisData(edat);

  XLALDestroyMultiSFTVector( inputSFTs);
  LALFree(pg1.data);
  
  XLALDestroyUserVars();

  LALCheckMemoryLeaks();

  if ( lalDebugLevel )
    REPORTSTATUS ( &status);

  return status.statusCode;
}



  


void ComputeFoft(LALStatus   *status,
		 REAL8Vector          *foft,
                 HoughTemplate        *pulsarTemplate,
		 REAL8Vector          *timeDiffV,
		 REAL8Cart3CoorVector *velV,
                 REAL8                 timeBase){
  
  INT4   mObsCoh;
  REAL8   f0new, vcProdn, timeDiffN;
  INT4    f0newBin;
  REAL8   sourceDelta, sourceAlpha, cosDelta;
  INT4    j,i, nspin, factorialN; 
  REAL8Cart3Coor  sourceLocation;
  
  /* --------------------------------------------- */
  INITSTATUS(status);
  ATTATCHSTATUSPTR (status);
  
  /*   Make sure the arguments are not NULL: */
  ASSERT (foft,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (pulsarTemplate,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (timeDiffV,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (velV,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  
  ASSERT (foft->data,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (timeDiffV->data,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  ASSERT (velV->data,  status, DRIVEHOUGHCOLOR_ENULL, DRIVEHOUGHCOLOR_MSGENULL);
  
  sourceDelta = pulsarTemplate->latitude;
  sourceAlpha = pulsarTemplate->longitude;
  cosDelta = cos(sourceDelta);
  
  sourceLocation.x = cosDelta* cos(sourceAlpha);
  sourceLocation.y = cosDelta* sin(sourceAlpha);
  sourceLocation.z = sin(sourceDelta);
    
  mObsCoh = foft->length;    
  nspin = pulsarTemplate->spindown.length;
  
  for (j=0; j<mObsCoh; ++j){  /* loop for all different time stamps */
    vcProdn = velV->data[j].x * sourceLocation.x
      + velV->data[j].y * sourceLocation.y
      + velV->data[j].z * sourceLocation.z;
    f0new = pulsarTemplate->f0;
    factorialN = 1;
    timeDiffN = timeDiffV->data[j];
    
    for (i=0; i<nspin;++i){ /* loop for spin-down values */
      factorialN *=(i+1);
      f0new += pulsarTemplate->spindown.data[i]* timeDiffN / factorialN;
      timeDiffN *= timeDiffN;
    }
    f0newBin = floor( f0new * timeBase + 0.5);
    foft->data[j] = f0newBin * (1.0 +vcProdn) / timeBase;
  }    
    
  DETATCHSTATUSPTR (status);
  /* normal exit */
  RETURN (status);
}


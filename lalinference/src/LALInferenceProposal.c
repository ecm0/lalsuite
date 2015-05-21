/*
 *  LALInferenceProposal.c:  Bayesian Followup, jump proposals.
 *
 *  Copyright (C) 2011 Ilya Mandel, Vivien Raymond, Christian Roever,
 *  Marc van der Sluys, John Veitch, Will M. Farr, Ben Farr
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <lal/LALInspiral.h>
#include <lal/DetResponse.h>
#include <lal/SeqFactories.h>
#include <lal/Date.h>
#include <lal/VectorOps.h>
#include <lal/TimeFreqFFT.h>
#include <lal/GenerateInspiral.h>
#include <lal/TimeDelay.h>
#include <lal/SkyCoordinates.h>
#include <lal/LALInference.h>
#include <lal/LALInferenceInit.h>
#include <lal/LALInferencePrior.h>
#include <lal/LALInferenceLikelihood.h>
#include <lal/LALInferenceTemplate.h>
#include <lal/LALInferenceProposal.h>
#include <lal/LALDatatypes.h>
#include <lal/FrequencySeries.h>
#include <lal/LALSimInspiral.h>
#include <lal/LALSimNoise.h>
#include <lal/XLALError.h>

#include <lal/LALStdlib.h>
#include <lal/LALInferenceClusteredKDE.h>
#include <lal/LALInferenceNestedSampler.h>

#ifdef __GNUC__
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

const char *const cycleArrayName = "Proposal Cycle";
const char *const cycleArrayLengthName = "Proposal Cycle Length";
const char *const cycleArrayCounterName = "Proposal Cycle Counter";

const char *const LALInferenceCurrentProposalName = "Current Proposal";

/* Proposal Names */
const char *const nullProposalName = "NULL";
const char *const singleAdaptProposalName = "Single";
const char *const singleProposalName = "Single";
const char *const orbitalPhaseJumpName = "OrbitalPhase";
const char *const covarianceEigenvectorJumpName = "CovarianceEigenvector";
const char *const skyLocWanderJumpName = "SkyLocWander";
const char *const differentialEvolutionFullName = "DifferentialEvolutionFull";
const char *const differentialEvolutionIntrinsicName = "DifferentialEvolutionIntrinsic";
const char *const differentialEvolutionExtrinsicName = "DifferentialEvolutionExtrinsic";
const char *const ensembleStretchFullName = "EnsembleStretchFull";
const char *const ensembleStretchIntrinsicName = "EnsembleStretchIntrinsic";
const char *const ensembleStretchExtrinsicName = "EnsembleStretchExtrinsic";
const char *const drawApproxPriorName = "DrawApproxPrior";
const char *const skyReflectDetPlaneName = "SkyReflectDetPlane";
const char *const skyRingProposalName = "SkyRingProposal";
const char *const PSDFitJumpName = "PSDFitJump";
const char *const polarizationPhaseJumpName = "PolarizationPhase";
const char *const polarizationCorrPhaseJumpName = "CorrPolarizationPhase";
const char *const extrinsicParamProposalName = "ExtrinsicParamProposal";
const char *const frequencyBinJumpName = "FrequencyBin";
const char *const GlitchMorletJumpName = "glitchMorletJump";
const char *const GlitchMorletReverseJumpName = "glitchMorletReverseJump";
const char *const ensembleWalkFullName = "EnsembleWalkFull";
const char *const ensembleWalkIntrinsicName = "EnsembleWalkIntrinsic";
const char *const ensembleWalkExtrinsicName = "EnsembleWalkExtrinsic";
const char *const clusteredKDEProposalName = "ClusteredKDEProposal";
const char *const splineCalibrationProposalName = "SplineCalibration";


static INT4 same_detector_location(LALDetector *d1, LALDetector *d2) {
    INT4 i;

    for (i = 0; i < 3; i++) {
        if (d1->location[i] != d2->location[i])
            return 0;
    }

    return 1;
}

static INT4 numDetectorsUniquePositions(LALInferenceIFOData *data) {
    INT4 nIFO = 0;
    INT4 nCollision = 0;
    LALInferenceIFOData *currentIFO = NULL;

    for (currentIFO = data; currentIFO; currentIFO = currentIFO->next) {
        LALInferenceIFOData *subsequentIFO = NULL;
        nIFO++;
        for (subsequentIFO = currentIFO->next; subsequentIFO; subsequentIFO = subsequentIFO->next) {
            if (same_detector_location(subsequentIFO->detector, currentIFO->detector)) {
                nCollision++;
                break;
            }
        }
    }

    return nIFO - nCollision;
}

LALInferenceProposal *LALInferenceInitProposal(LALInferenceProposalFunction func, const char *name)
{
  LALInferenceProposal *proposal = XLALCalloc(1,sizeof(LALInferenceProposal));
  proposal->func = func;
  proposal->proposed = 0;
  proposal->accepted = 0;
  strcpy(proposal->name, name);
  return proposal;
}


void LALInferenceRegisterProposal(LALInferenceVariables *propArgs, const char *name, INT4 *flag, ProcessParamsTable *command_line) {
    char offopt[VARNAME_MAX+15];
    char onopt[VARNAME_MAX+12];

    sprintf(offopt, "--proposal-no-%s", name);
    sprintf(onopt, "--proposal-%s", name);

    if (LALInferenceGetProcParamVal(command_line, offopt))
        *flag = 0;
    else if (LALInferenceGetProcParamVal(command_line, onopt))
        *flag = 1;

    LALInferenceAddINT4Variable(propArgs, name, *flag, LALINFERENCE_PARAM_FIXED);
}

void LALInferenceAddProposalToCycle(LALInferenceProposalCycle *cycle, LALInferenceProposal *prop, INT4 weight) {
    const char *fname = "LALInferenceAddProposalToCycle";
    INT4 i;

    /* Quit without doing anything if weight = 0. */
    if (weight == 0)
        return;

    cycle->order = XLALRealloc(cycle->order, (cycle->length + weight)*sizeof(INT4));
    if (cycle->order == NULL) {
        XLALError(fname, __FILE__, __LINE__, XLAL_ENOMEM);
        exit(1);
    }

    for (i = cycle->length; i < cycle->length + weight; i++) {
        cycle->order[i] = cycle->nProposals;
    }

    cycle->proposals = XLALRealloc(cycle->proposals, (cycle->nProposals)*sizeof(LALInferenceProposal));
    if (cycle->proposals == NULL) {
        XLALError(fname, __FILE__, __LINE__, XLAL_ENOMEM);
        exit(1);
    }
    cycle->proposals[cycle->nProposals] = prop;

    cycle->length += weight;
    cycle->nProposals += 1;
}



void LALInferenceRandomizeProposalCycle(LALInferenceProposalCycle *cycle, gsl_rng *rng) {
    INT4 i, j, temp;

    for (i = cycle->length - 1; i > 0; i--) {
        /* Fill in array from right to left, chosen randomly from remaining proposals. */
        j = gsl_rng_uniform_int(rng, i+1);

        temp = cycle->order[j];
        cycle->order[j] = cycle->order[i];
        cycle->order[i] = temp;
    }
}


REAL8 LALInferenceCyclicProposal(LALInferenceThreadState *thread,
                                 LALInferenceVariables *currentParams,
                                 LALInferenceVariables *proposedParams) {
    INT4 i = 0;
    LALInferenceProposalCycle *cycle=NULL;

    /* Must have cycle array and cycle array length in propArgs. */
    cycle = thread->cycle;
    if (cycle == NULL) {
        XLALError("LALInferenceCyclicProposal()",__FILE__,__LINE__,XLAL_FAILURE);
        exit(1);
    }

    if (cycle->counter >= cycle->length) {
        XLALError("LALInferenceCyclicProposal()",__FILE__,__LINE__,XLAL_FAILURE);
        exit(1);
    }

    /* One instance of each proposal object is stored in cycle->proposals.
        cycle->order is a list of elements to call from the proposals */
    i = cycle->order[cycle->counter];
    REAL8 logPropRatio = cycle->proposals[i]->func(thread, currentParams, proposedParams);
    strcpy(cycle->last_proposal, cycle->proposals[i]->name);

    /* Call proposals until one succeeds */
    while (proposedParams->head == NULL) {
        LALInferenceClearVariables(proposedParams);

        i = cycle->order[cycle->counter];
        logPropRatio = cycle->proposals[i]->func(thread, currentParams, proposedParams);
        strcpy(cycle->last_proposal, cycle->proposals[i]->name);

        /* Increment counter for the next time around. */
        cycle->counter = (cycle->counter + 1) % cycle->length;
    }

    /* Increment counter for the next time around. */
    cycle->counter = (cycle->counter + 1) % cycle->length;

    return logPropRatio;
}

LALInferenceProposalCycle* LALInferenceInitProposalCycle(void) {
  LALInferenceProposalCycle *cycle = XLALCalloc(1,sizeof(LALInferenceProposalCycle));
  return cycle;
}

void LALInferenceDeleteProposalCycle(LALInferenceProposalCycle *cycle) {
    XLALFree(cycle->proposals);
    XLALFree(cycle->order);
}

LALInferenceVariables *LALInferenceParseProposalArgs(LALInferenceRunState *runState) {
    INT4 i;
    ProcessParamsTable *ppt;
    LALInferenceIFOData *ifo = runState->data;

    LALInferenceVariables *propArgs = XLALCalloc(1, sizeof(LALInferenceVariables));

    INT4 Nskip = 1;
    INT4 noise_only = 0;
    INT4 cyclic_reflective_kde = 0;

    /* Flags for proposals, initialized with the MCMC defaults */

    INT4 singleadapt = 0; /* Disabled for bug checking */
    INT4 psiphi = 1;
    INT4 ext_param = 1;
    INT4 skywander = 1;
    INT4 skyreflect = 1;
    INT4 drawprior = 1;
    INT4 covjump = 0;
    INT4 diffevo = 1;
    INT4 stretch = 1;
    INT4 walk = 0;
    INT4 skyring = 1;
    INT4 kde = 0;
    INT4 spline_cal = 0;
    INT4 psdfit = 0;
    INT4 glitchfit = 0;

    if (runState->algorithm == &LALInferenceNestedSamplingAlgorithm) {
        singleadapt = 0;
        psiphi = 0;
        ext_param = 0;
        skywander = 0;
        skyreflect = 0;
        drawprior = 0;
        covjump = 1;
        diffevo = 1;
        stretch = 1;
        walk = 1;
        skyring = 0;
        kde = 0;
        spline_cal = 0;
        psdfit = 0;
        glitchfit = 0;
    }

    ProcessParamsTable *command_line = runState->commandLine;

    LIGOTimeGPS epoch = ifo->epoch;
    LALInferenceAddVariable(propArgs, "epoch", &(epoch), LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);

    /* Determine the number of iterations between each entry in the DE buffer */
    if (LALInferenceCheckVariable(runState->algorithmParams, "Nskip"))
        Nskip = LALInferenceGetINT4Variable(runState->algorithmParams, "Nskip");
    LALInferenceAddINT4Variable(propArgs, "Nskip", Nskip, LALINFERENCE_PARAM_FIXED);

    /* Count the number of IFOs and uniquely-located IFOs to decide which sky-related proposals to use */
    INT4 nDet = 0;
    ifo=runState->data;
    while (ifo) {
        nDet++;
        ifo = ifo->next;
    }
    LALInferenceAddINT4Variable(propArgs, "nDet", nDet, LALINFERENCE_PARAM_FIXED);

    INT4 nUniqueDet = numDetectorsUniquePositions(runState->data);
    LALInferenceAddINT4Variable(propArgs, "nUniqueDet", nUniqueDet, LALINFERENCE_PARAM_FIXED);

    LALDetector *detectors = XLALCalloc(nDet, sizeof(LALDetector));
    for (i=0,ifo=runState->data; i<nDet; i++)
        detectors[i] = *(ifo->detector);
    LALInferenceAddVariable(propArgs, "detectors", &detectors, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);

    char **ifo_names = XLALCalloc(nDet, sizeof(char*));
    for(ifo=runState->data,i=0;ifo;ifo=ifo->next,i++) {
        ifo_names[i] = XLALCalloc(DETNAMELEN, sizeof(char));
        strcpy(ifo_names[i], ifo->name);
    }
    LALInferenceAddVariable(propArgs, "detector_names", ifo_names, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);

    INT4 marg_timephi = 0;
    if (LALInferenceGetProcParamVal(command_line, "--margtimephi"))
        marg_timephi = 1;

    INT4 marg_time = 0;
    if (marg_timephi || LALInferenceGetProcParamVal(command_line, "--margtime"))
        marg_time = 1;
    LALInferenceAddINT4Variable(propArgs, "marg_time", marg_time, LALINFERENCE_PARAM_FIXED);

    INT4 marg_phi = 0;
    if (marg_timephi || LALInferenceGetProcParamVal(command_line, "--margphi"))
        marg_phi = 1;
    LALInferenceAddINT4Variable(propArgs, "marg_phi", marg_phi, LALINFERENCE_PARAM_FIXED);

    INT4 analytic_test = 0;
    if (LALInferenceGetProcParamVal(command_line, "--correlatedGaussianLikelihood") ||
        LALInferenceGetProcParamVal(command_line, "--bimodalGaussianLikelihood") ||
        LALInferenceGetProcParamVal(command_line, "--rosenbrockLikelihood")) {
        analytic_test = 1;
    }
    LALInferenceAddINT4Variable(propArgs, "analytical_test", analytic_test, LALINFERENCE_PARAM_FIXED);

    INT4 skyframe = 1;
    if (LALInferenceGetProcParamVal(command_line, "--no-sky-frame"))
        skyframe = 0;

    INT4 noAdapt = 0;
    if (LALInferenceGetProcParamVal(command_line, "--no-adapt"))
        noAdapt = 1;
    INT4 adapting = !noAdapt;
    LALInferenceAddINT4Variable(propArgs, "no_adapt", noAdapt, LALINFERENCE_PARAM_LINEAR);
    LALInferenceAddINT4Variable(propArgs, "adapting", adapting, LALINFERENCE_PARAM_LINEAR);

    INT4 tau = 5;
    ppt = LALInferenceGetProcParamVal(command_line, "--adaptTau");
    if (ppt)
        tau = atof(ppt->value);
    LALInferenceAddINT4Variable(propArgs, "adaptTau", tau, LALINFERENCE_PARAM_FIXED);

    INT4 sampling_prior = 0;
    ppt = LALInferenceGetProcParamVal(command_line, "--zerologlike");
    if (ppt)
        sampling_prior = 1;
    LALInferenceAddINT4Variable(propArgs, "sampling_prior", sampling_prior, LALINFERENCE_PARAM_FIXED);

    if (LALInferenceGetProcParamVal(command_line, "--enable-spline-calibration"))
        spline_cal = 1;

    if (LALInferenceGetProcParamVal(command_line, "--psd-fit"))
        psdfit = 1;

    if (LALInferenceGetProcParamVal(command_line, "--glitch-fit"))
       glitchfit = 1;

    /* Check if imposing cyclic reflective bounds */
    if (LALInferenceGetProcParamVal(runState->commandLine, "--cyclic-reflective-kde"))
        cyclic_reflective_kde = 1;
    LALInferenceAddINT4Variable(propArgs, "cyclic_reflective_kde", cyclic_reflective_kde, LALINFERENCE_PARAM_FIXED);

    if (LALInferenceGetProcParamVal(command_line, "--noiseonly"))
        noise_only = 1;
    LALInferenceAddINT4Variable(propArgs, "noiseonly", noise_only, LALINFERENCE_PARAM_FIXED);

    /* Turn off signal proposals if no signal is in the model */
    if (noise_only) {
        singleadapt = 0;
        psiphi = 0;
        ext_param = 0;
        skywander = 0;
        skyreflect = 0;
        drawprior = 0;
        covjump = 0;
        diffevo = 0;
        stretch = 0;
        walk = 0;
        skyring = 0;
        spline_cal = 0;
    }

    /* Turn off phi-related proposals if marginalizing over phi in likelihood */
    if (marg_phi) {
        psiphi = 0;
    }

    /* Disable proposals that won't work with the current number of unique detectors */
    if (nUniqueDet < 2) {
        skyring = 0;
    }

    if (nUniqueDet != 3) {
        skyreflect = 0;
    }

    if (nUniqueDet >= 3) {
        ext_param = 0;
    }

    /* Turn off ra-dec related proposals when using the sky-frame coordinate system */
    if (skyframe) {
        ext_param = 0;
        skywander = 0;
        skyreflect = 0;
        skyring = 0;
    }

    /* Register all proposal functions, check for explicit command-line requests */
    LALInferenceRegisterProposal(propArgs, "singleadapt", &singleadapt, command_line);
    LALInferenceRegisterProposal(propArgs, "psiphi", &psiphi, command_line);
    LALInferenceRegisterProposal(propArgs, "extrinsicparam", &ext_param, command_line);
    LALInferenceRegisterProposal(propArgs, "skywander", &skywander, command_line);
    LALInferenceRegisterProposal(propArgs, "skyreflect", &skyreflect, command_line);
    LALInferenceRegisterProposal(propArgs, "drawprior", &drawprior, command_line);
    LALInferenceRegisterProposal(propArgs, "eigenvectors", &covjump, command_line);
    LALInferenceRegisterProposal(propArgs, "differentialevolution", &diffevo, command_line);
    LALInferenceRegisterProposal(propArgs, "stretch", &stretch, command_line);
    LALInferenceRegisterProposal(propArgs, "walk", &walk, command_line);
    LALInferenceRegisterProposal(propArgs, "skyring", &skyring, command_line);
    LALInferenceRegisterProposal(propArgs, "kde", &kde, command_line);
    LALInferenceRegisterProposal(propArgs, "spline_cal", &spline_cal, command_line);
    LALInferenceRegisterProposal(propArgs, "psdfit", &psdfit, command_line);
    LALInferenceRegisterProposal(propArgs, "glitchfit", &glitchfit, command_line);

    /* Setup adaptive proposals */
    LALInferenceModel *model = LALInferenceInitCBCModel(runState);
    LALInferenceSetupAdaptiveProposals(propArgs, model->params);
    XLALFree(model);

    /* Setup buffer now since threads aren't accessible to the main setup function */
    if (diffevo || stretch || walk) {
        for (i=0; i<runState->nthreads; i++)
            LALInferenceSetupDifferentialEvolutionProposal(runState->threads[i]);
    }

    /* Setup now since we need access to the data */
    if (glitchfit)
        LALInferenceSetupGlitchProposal(runState->data, propArgs);

    return propArgs;
}


LALInferenceProposalCycle* LALInferenceSetupDefaultInspiralProposalCycle(LALInferenceVariables *propArgs) {
    LALInferenceProposal *prop;

    const INT4 BIGWEIGHT = 20;
    const INT4 SMALLWEIGHT = 5;
    const INT4 TINYWEIGHT = 1;

    LALInferenceProposalCycle *cycle = XLALCalloc(1, sizeof(LALInferenceProposalCycle));

    if (LALInferenceGetINT4Variable(propArgs, "singleadapt")) {
        prop = LALInferenceInitProposal(&LALInferenceSingleAdaptProposal, singleAdaptProposalName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "psiphi")) {
        prop = LALInferenceInitProposal(&LALInferencePolarizationPhaseJump, polarizationPhaseJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, TINYWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "extrinsicparam")) {
        prop = LALInferenceInitProposal(&LALInferenceExtrinsicParamProposal, extrinsicParamProposalName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "skywander")) {
        prop = LALInferenceInitProposal(&LALInferenceSkyLocWanderJump, skyLocWanderJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "skyreflect")) {
        prop = LALInferenceInitProposal(&LALInferenceSkyReflectDetPlane, skyReflectDetPlaneName);
        LALInferenceAddProposalToCycle(cycle, prop, TINYWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "drawprior")) {
        prop = LALInferenceInitProposal(&LALInferenceDrawApproxPrior, drawApproxPriorName);
        LALInferenceAddProposalToCycle(cycle, prop, TINYWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "eigenvectors")) {
        prop = LALInferenceInitProposal(&LALInferenceCovarianceEigenvectorJump, covarianceEigenvectorJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "differentialevolution")) {
        prop = LALInferenceInitProposal(&LALInferenceDifferentialEvolutionFull, differentialEvolutionFullName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceDifferentialEvolutionIntrinsic, differentialEvolutionIntrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceDifferentialEvolutionExtrinsic, differentialEvolutionExtrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "stretch")) {
        prop = LALInferenceInitProposal(&LALInferenceEnsembleStretchFull, ensembleStretchFullName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceEnsembleStretchIntrinsic, ensembleStretchIntrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceEnsembleStretchExtrinsic, ensembleStretchExtrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "walk")) {
        prop = LALInferenceInitProposal(&LALInferenceEnsembleWalkFull, ensembleWalkFullName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceEnsembleWalkIntrinsic, ensembleWalkIntrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceEnsembleWalkExtrinsic, ensembleWalkExtrinsicName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "skyring")) {
        prop = LALInferenceInitProposal(&LALInferenceSkyRingProposal, skyRingProposalName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "kde")) {
        prop = LALInferenceInitProposal(&LALInferenceClusteredKDEProposal, clusteredKDEProposalName);
        LALInferenceAddProposalToCycle(cycle, prop, BIGWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "spline_cal")) {
        prop = LALInferenceInitProposal(&LALInferenceSplineCalibrationProposal, splineCalibrationProposalName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "psdfit")) {
        prop = LALInferenceInitProposal(&LALInferencePSDFitJump, PSDFitJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    if (LALInferenceGetINT4Variable(propArgs, "glitchfit")) {
        prop = LALInferenceInitProposal(&LALInferenceGlitchMorletProposal, GlitchMorletJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);

        prop = LALInferenceInitProposal(&LALInferenceGlitchMorletReverseJump, GlitchMorletReverseJumpName);
        LALInferenceAddProposalToCycle(cycle, prop, SMALLWEIGHT);
    }

    return cycle;
}


REAL8 LALInferenceSingleAdaptProposal(LALInferenceThreadState *thread,
                                      LALInferenceVariables *currentParams,
                                      LALInferenceVariables *proposedParams) {
    gsl_matrix *m=NULL;
    INT4Vector *v=NULL;
    INT4 dim, varNr;
    INT4 i = 0;
    REAL8 logPropRatio, sqrttemp, sigma;
    char tmpname[MAX_STRLEN] = "";
    LALInferenceVariableItem *param = NULL, *dummyParam = NULL;

    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng *rng = thread->GSLrandom;

    if (!LALInferenceGetINT4Variable(args, "no_adapt")) {
        if (!LALInferenceCheckVariable(args, "adapting"))
            LALInferenceSetupAdaptiveProposals(args, currentParams);

        sqrttemp = sqrt(thread->temperature);
        dim = proposedParams->dimension;

        do {
            varNr = 1 + gsl_rng_uniform_int(rng, dim);
            param = LALInferenceGetItemNr(proposedParams, varNr);
        } while (!LALInferenceCheckVariableNonFixed(proposedParams, param->name) || param->type != LALINFERENCE_REAL8_t);

        for (dummyParam = proposedParams->head; dummyParam != NULL; dummyParam = dummyParam->next) {
            if (!strcmp(dummyParam->name, param->name)) {
                /* Found it; i = index into sigma vector. */
                break;
            } else if (!LALInferenceCheckVariableNonFixed(proposedParams, dummyParam->name)) {
                /* Don't increment i, since we're not dealing with a "real" parameter. */
                continue;
            } else if (param->type == LALINFERENCE_gslMatrix_t) {
                /*increment i by number of noise parameters, since they aren't included in adaptive jumps*/
                m = *((gsl_matrix **)dummyParam->value);
                i += (int)( m->size1*m->size2 );
            } else if (param->type == LALINFERENCE_INT4Vector_t) {
                /*
                 increment i by number of size of vectors --
                 number of wavelets in glitch model is not
                 part of adaptive proposal
                 */
                v = *((INT4Vector **)dummyParam->value);
                i += (int)( v->length );
            } else {
                i++;
                continue;
            }
        }

        if (param->type != LALINFERENCE_REAL8_t) {
            fprintf(stderr, "Attempting to set non-REAL8 parameter with numerical sigma (in %s, %d)\n",
                    __FILE__, __LINE__);
            exit(1);
        }

        sprintf(tmpname,"%s_%s",param->name,ADAPTSUFFIX);
        if (!LALInferenceCheckVariable(thread->proposalArgs, tmpname)) {
            fprintf(stderr, "Attempting to draw single-parameter jump for %s but cannot find sigma!\nError in %s, line %d.\n",
                    param->name,__FILE__, __LINE__);
            exit(1);
        }

        sigma = LALInferenceGetREAL8Variable(thread->proposalArgs, tmpname);

        /* Save the name of the proposed variable */
        if(LALInferenceCheckVariable(args, "proposedVariableName")){
            char *nameBuffer = *(char **)LALInferenceGetVariable(args, "proposedVariableName");
            strncpy(nameBuffer, param->name, MAX_STRLEN-1);
        }

        *((REAL8 *)param->value) += gsl_ran_ugaussian(rng) * sigma * sqrttemp;

        LALInferenceCyclicReflectiveBound(proposedParams, thread->priorArgs);

        /* Set the log of the proposal ratio to zero, since this is a
        symmetric proposal. */
        logPropRatio = 0.0;

        INT4 as = 1;
        LALInferenceSetVariable(args, "adaptableStep", &as);

    } else {
        /* We are not adaptive, or for some reason don't have a sigma
           vector---fall back on old proposal. */
        logPropRatio = LALInferenceSingleProposal(thread, currentParams, proposedParams);
    }

    return logPropRatio;
}

REAL8 LALInferenceSingleProposal(LALInferenceThreadState *thread,
                                 LALInferenceVariables *currentParams,
                                 LALInferenceVariables *proposedParams) {
    LALInferenceVariableItem *param=NULL, *dummyParam=NULL;
    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng * GSLrandom = thread->GSLrandom;
    REAL8 sigma, big_sigma;
    INT4 i, dim, varNr;

    LALInferenceCopyVariables(currentParams, proposedParams);

    sigma = 0.1 * sqrt(thread->temperature); /* Adapt step to temperature. */
    big_sigma = 1.0;

    if (gsl_ran_ugaussian(GSLrandom) < 1.0e-3)
        big_sigma = 1.0e1;    //Every 1e3 iterations, take a 10x larger jump in a parameter
    if (gsl_ran_ugaussian(GSLrandom) < 1.0e-4)
        big_sigma = 1.0e2;    //Every 1e4 iterations, take a 100x larger jump in a parameter

    dim = proposedParams->dimension;

    do {
        varNr = 1 + gsl_rng_uniform_int(GSLrandom, dim);
        param = LALInferenceGetItemNr(proposedParams, varNr);
    } while (!LALInferenceCheckVariableNonFixed(proposedParams, param->name) || param->type != LALINFERENCE_REAL8_t);

    i = 0;
    for (dummyParam = proposedParams->head; dummyParam != NULL; dummyParam = dummyParam->next) {
        if (!strcmp(dummyParam->name, param->name)) {
            /* Found it; i = index into sigma vector. */
            break;
        } else if (!LALInferenceCheckVariableNonFixed(proposedParams, param->name) || param->type != LALINFERENCE_REAL8_t) {
            /* Don't increment i, since we're not dealing with a "real" parameter. */
            continue;
        } else {
            i++;
            continue;
        }
    }

    /* Scale jumps proposal appropriately for prior sampling */
    if (LALInferenceGetINT4Variable(args, "sampling_prior")) {
        if (!strcmp(param->name, "eta")) {
            sigma = 0.02;
        } else if (!strcmp(param->name, "q")) {
            sigma = 0.08;
        } else if (!strcmp(param->name, "chirpmass")) {
            sigma = 1.0;
        } else if (!strcmp(param->name, "time")) {
            sigma = 0.02;
        } else if (!strcmp(param->name, "phase")) {
            sigma = 0.6;
        } else if (!strcmp(param->name, "distance")) {
            sigma = 10.0;
        } else if (!strcmp(param->name, "declination")) {
            sigma = 0.3;
        } else if (!strcmp(param->name, "rightascension")) {
            sigma = 0.6;
        } else if (!strcmp(param->name, "polarisation")) {
            sigma = 0.6;
        } else if (!strcmp(param->name,"costheta_jn")) {
            sigma = 0.3;
        } else if (!strcmp(param->name, "a_spin1")) {
            sigma = 0.1;
        } else if (!strcmp(param->name, "a_spin2")) {
            sigma = 0.1;
        } else {
            fprintf(stderr, "Could not find parameter %s!", param->name);
            exit(1);
        }

        *(REAL8 *)param->value += gsl_ran_ugaussian(GSLrandom)*sigma;
    } else {
        if (!strcmp(param->name,"eta") || !strcmp(param->name,"q") || !strcmp(param->name,"time") || !strcmp(param->name,"a_spin2") || !strcmp(param->name,"a_spin1")){
            *(REAL8 *)param->value += gsl_ran_ugaussian(GSLrandom)*big_sigma*sigma*0.001;
        } else if (!strcmp(param->name,"polarisation") || !strcmp(param->name,"phase") || !strcmp(param->name,"costheta_jn")){
            *(REAL8 *)param->value += gsl_ran_ugaussian(GSLrandom)*big_sigma*sigma*0.1;
        } else {
            *(REAL8 *)param->value += gsl_ran_ugaussian(GSLrandom)*big_sigma*sigma*0.01;
        }
    }

    LALInferenceCyclicReflectiveBound(proposedParams, thread->priorArgs);

    /* Symmetric Proposal. */
    REAL8 logPropRatio = 0.0;

    return logPropRatio;
}


REAL8 LALInferenceCovarianceEigenvectorJump(LALInferenceThreadState *thread,
                                            LALInferenceVariables *currentParams,
                                            LALInferenceVariables *proposedParams) {
    LALInferenceVariableItem *proposeIterator;
    REAL8Vector *eigenvalues;
    gsl_matrix *eigenvectors;
    REAL8 jumpSize, tmp, inc;
    REAL8 logPropRatio = 0.0;
    INT4 N, i, j;

    LALInferenceCopyVariables(currentParams, proposedParams);

    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng *rng = thread->GSLrandom;

    eigenvalues = LALInferenceGetREAL8VectorVariable(args, "covarianceEigenvalues");
    eigenvectors = LALInferenceGetgslMatrixVariable(args, "covarianceEigenvectors");

    N = eigenvalues->length;

    i = gsl_rng_uniform_int(rng, N);
    jumpSize = sqrt(thread->temperature * eigenvalues->data[i]) * gsl_ran_ugaussian(rng);

    j = 0;
    proposeIterator = proposedParams->head;
    if (proposeIterator == NULL) {
        fprintf(stderr, "Bad proposed params in %s, line %d\n",
                __FILE__, __LINE__);
        exit(1);
    }

    do {
        if (LALInferenceCheckVariableNonFixed(proposedParams, proposeIterator->name) &&
            proposeIterator->type==LALINFERENCE_REAL8_t) {
            tmp = LALInferenceGetREAL8Variable(proposedParams, proposeIterator->name);
            inc = jumpSize * gsl_matrix_get(eigenvectors, j, i);

            tmp += inc;

            LALInferenceSetVariable(proposedParams, proposeIterator->name, &tmp);

            j++;
        }
    } while ((proposeIterator = proposeIterator->next) != NULL && j < N);

    return logPropRatio;
}

REAL8 LALInferenceSkyLocWanderJump(LALInferenceThreadState *thread,
                                   LALInferenceVariables *currentParams,
                                   LALInferenceVariables *proposedParams) {
    REAL8 sigma;
    REAL8 jumpX, jumpY;
    REAL8 RA, DEC;
    REAL8 newRA, newDEC;
    REAL8 one_deg = 1.0 / (2.0*M_PI);
    REAL8 logPropRatio = 0.0;

    LALInferenceCopyVariables(currentParams, proposedParams);

    gsl_rng *rng = thread->GSLrandom;

    sigma = sqrt(thread->temperature) * one_deg;
    jumpX = sigma * gsl_ran_ugaussian(rng);
    jumpY = sigma * gsl_ran_ugaussian(rng);

    RA = LALInferenceGetREAL8Variable(proposedParams, "rightascension");
    DEC = LALInferenceGetREAL8Variable(proposedParams, "declination");

    newRA = RA + jumpX;
    newDEC = DEC + jumpY;

    LALInferenceSetVariable(proposedParams, "rightascension", &newRA);
    LALInferenceSetVariable(proposedParams, "declination", &newDEC);


    return logPropRatio;
}

REAL8 LALInferenceDifferentialEvolutionFull(LALInferenceThreadState *thread,
                                            LALInferenceVariables *currentParams,
                                            LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, NULL);

    return logPropRatio;
}

REAL8 LALInferenceEnsembleStretchFull(LALInferenceThreadState *thread,
                                      LALInferenceVariables *currentParams,
                                      LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, NULL);

    return logPropRatio;
}


REAL8 LALInferenceEnsembleStretchIntrinsic(LALInferenceThreadState *thread,
                                           LALInferenceVariables *currentParams,
                                           LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    const char *names[] = {"chirpmass", "q", "eta", "m1", "m2", "a_spin1", "a_spin2",
                           "tilt_spin1", "tilt_spin2", "phi12", NULL};

    logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}

REAL8 LALInferenceEnsembleStretchExtrinsic(LALInferenceThreadState *thread,
                                           LALInferenceVariables *currentParams,
                                           LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    INT4 margtime, margphi;

    LALInferenceVariables *args = thread->proposalArgs;

    const char *names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                           "phase", "time", "costheta_jn", "theta", "cosalpha", "t0", NULL};

    const char *marg_time_names[] = {"rightascension", "declination", "polarisation","distance", "logdistance",
                                     "phase","costheta_jn", "theta", "cosalpha", "t0", NULL};

    const char *marg_phase_names[] = {"rightascension", "declination", "polarisation","distance", "logdistance",
                                      "time","costheta_jn", "theta", "cosalpha", "t0", NULL};


    const char *marg_time_phase_names[] = {"rightascension", "declination", "polarisation",  "distance", "logdistance",
                                           "costheta_jn", "theta", "cosalpha", "t0",  NULL};

    margtime = LALInferenceGetINT4Variable(args, "marg_time");
    margphi = LALInferenceGetINT4Variable(args, "marg_phi");

    if (margtime && margphi)
        logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, marg_time_phase_names);
    else if (margtime)
        logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, marg_time_names);
    else if (margphi)
        logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, marg_phase_names);
    else
        logPropRatio = LALInferenceEnsembleStretchNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}

/* This jump uses the current sample 'A' and another randomly
 * drawn 'B' from the ensemble of live points, and proposes
 * C = B+Z(A-B) where Z is a scale factor */
REAL8 LALInferenceEnsembleStretchNames(LALInferenceThreadState *thread,
                                       LALInferenceVariables *currentParams,
                                       LALInferenceVariables *proposedParams,
                                       const char **names) {
    size_t i, N, Ndim, nPts;
    REAL8 logPropRatio;
    REAL8 maxScale, Y, logmax, X, scale;
    REAL8 cur, other, x;
    LALInferenceVariableItem *item;
    LALInferenceVariables **dePts;
    LALInferenceVariables *ptI;

    LALInferenceCopyVariables(currentParams, proposedParams);

    if (names == NULL) {
        N = LALInferenceGetVariableDimension(currentParams) + 1; /* More names than we need. */
        names = alloca(N * sizeof(char *)); /* Hope we have alloca---saves
                                               having to deallocate after
                                               proposal. */

        item = currentParams->head;
        i = 0;
        while (item != NULL) {
            if (LALInferenceCheckVariableNonFixed(currentParams, item->name) && item->type==LALINFERENCE_REAL8_t ) {
                names[i] = item->name;
                i++;
            }
            item = item->next;
        }
        names[i]=NULL; /* Terminate */
    }

    Ndim = 0;
    for(Ndim=0, i=0; names[i] != NULL; i++ ) {
        if (LALInferenceCheckVariableNonFixed(proposedParams, names[i]))
            Ndim++;
    }

    dePts = thread->differentialPoints;
    nPts = thread->differentialPointsLength;

    if (dePts == NULL || nPts <= 1) {
        logPropRatio = 0.0;
        return logPropRatio; /* Quit now, since we don't have any points to use. */
    }

    i = gsl_rng_uniform_int(thread->GSLrandom, nPts);

    /* Choose a different sample */
    do {
        i = gsl_rng_uniform_int(thread->GSLrandom, nPts);
    } while (!LALInferenceCompareVariables(proposedParams, dePts[i]));

    ptI = dePts[i];

    /* Scale z is chosen according to be symmetric under z -> 1/z */
    /* so p(x) \propto 1/z between 1/a and a */

    /* TUNABLE PARAMETER (a), must be >1. Larger value -> smaller acceptance */
    maxScale=3.0;

    /* Draw sample between 1/max and max */
    Y = gsl_rng_uniform(thread->GSLrandom);
    logmax = log(maxScale);
    X = 2.0*logmax*Y - logmax;
    scale = exp(X);

    for (i = 0; names[i] != NULL; i++) {
        /* Ignore variable if it's not in each of the params. */
        if (LALInferenceCheckVariableNonFixed(proposedParams, names[i]) && LALInferenceCheckVariableNonFixed(ptI, names[i])) {
            cur = LALInferenceGetREAL8Variable(proposedParams, names[i]);
            other= LALInferenceGetREAL8Variable(ptI, names[i]);
            x = other + scale*(cur-other);

            LALInferenceSetVariable(proposedParams, names[i], &x);
        }
    }

    if (scale<maxScale && scale>(1.0/maxScale))
        logPropRatio = log(scale)*((REAL8)Ndim);
    else
        logPropRatio = -DBL_MAX;

    return logPropRatio;
}


REAL8 LALInferenceEnsembleWalkFull(LALInferenceThreadState *thread,
                                   LALInferenceVariables *currentParams,
                                   LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, NULL);

    return logPropRatio;
}


REAL8 LALInferenceEnsembleWalkIntrinsic(LALInferenceThreadState *thread,
                                        LALInferenceVariables *currentParams,
                                        LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    const char *names[] = {"chirpmass", "q", "eta", "m1", "m2", "a_spin1", "a_spin2",
                           "tilt_spin1", "tilt_spin2", "phi12", NULL};

    logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}

REAL8 LALInferenceEnsembleWalkExtrinsic(LALInferenceThreadState *thread,
                                        LALInferenceVariables *currentParams,
                                        LALInferenceVariables *proposedParams) {

    REAL8 logPropRatio;
    INT4 margtime, margphi;

    LALInferenceVariables *args = thread->proposalArgs;

    const char *names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                           "phase", "time", "costheta_jn", "theta", "cosalpha", "t0", NULL};

    const char *marg_time_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                     "phase", "costheta_jn", "theta", "cosalpha", "t0", NULL};

    const char *marg_phase_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                      "time", "costheta_jn", "theta", "cosalpha", "t0", NULL};

    const char *marg_time_phase_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                           "costheta_jn",  "theta", "cosalpha", "t0",NULL};

    margtime = LALInferenceGetINT4Variable(args, "marg_time");
    margphi = LALInferenceGetINT4Variable(args, "marg_phi");

    if (margtime && margphi)
        logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, marg_time_phase_names);
    else if (margtime)
        logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, marg_time_names);
    else if (margphi)
        logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, marg_phase_names);
    else
        logPropRatio = LALInferenceEnsembleWalkNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}


REAL8 LALInferenceEnsembleWalkNames(LALInferenceThreadState *thread,
                                    LALInferenceVariables *currentParams,
                                    LALInferenceVariables *proposedParams,
                                    const char **names) {
    size_t i, N, Ndim, nPts;
    size_t k, D, sample_size;
    LALInferenceVariableItem *item;
    LALInferenceVariables **dePts, **pointsPool;
    INT4 *indeces, *all_indeces;
    REAL8 *center_of_mass, *w, *univariate_normals;
    REAL8 tmp;
    REAL8 logPropRatio = 0.0;

    LALInferenceCopyVariables(currentParams, proposedParams);

    gsl_rng *rng = thread->GSLrandom;

    if (names == NULL) {
        N = LALInferenceGetVariableDimension(currentParams) + 1; /* More names than we need. */
        names = alloca(N*sizeof(char *)); /* Hope we have alloca---saves
                                             having to deallocate after
                                             proposal. */

        item = currentParams->head;
        i = 0;
        while (item != NULL) {
            if (LALInferenceCheckVariableNonFixed(currentParams, item->name) && item->type==LALINFERENCE_REAL8_t ) {
                names[i] = item->name;
                i++;
            }
            item = item->next;
        }
        names[i]=NULL; /* Terminate */
    }


    Ndim = 0;
    for(Ndim=0, i=0; names[i] != NULL; i++ ) {
        if(LALInferenceCheckVariableNonFixed(proposedParams, names[i]))
            Ndim++;
    }

    pointsPool = thread->differentialPoints;
    k=0;
    D = Ndim;
    sample_size = 3;

    dePts = thread->differentialPoints;
    nPts = thread->differentialPointsLength;

    if (dePts == NULL || nPts <= 1)
        return logPropRatio; /* Quit now, since we don't have any points to use. */

    indeces = alloca(sample_size * sizeof(int));
    all_indeces = alloca(nPts * sizeof(int));

    for (i=0; i<nPts; i++)
        all_indeces[i] = i;

    gsl_ran_choose(rng, indeces, sample_size, all_indeces, nPts, sizeof(INT4));

    center_of_mass = alloca(Ndim * sizeof(REAL8));
    w = alloca(Ndim * sizeof(REAL8));

    for (k=0; k<Ndim; k++) {
        center_of_mass[k] = 0.0;
        w[k] = 0.0;
    }

    for (i=0; i<sample_size; i++) {
        for(k=0; names[k]!=NULL; k++) {
            if (LALInferenceCheckVariableNonFixed(proposedParams, names[k]))
                center_of_mass[k] += LALInferenceGetREAL8Variable(pointsPool[indeces[i]], names[k])/((REAL8)sample_size);
        }
    }

    univariate_normals = alloca(D * sizeof(double));
    for (i=0; i<sample_size; i++)
        univariate_normals[i] = gsl_ran_ugaussian(rng);

    for (i=0; i<sample_size; i++) {
        for(k=0; names[k]!=NULL; k++) {
            if (LALInferenceCheckVariableNonFixed(proposedParams, names[k]) )
                w[k]+= (LALInferenceGetREAL8Variable(pointsPool[indeces[i]], names[k]) - center_of_mass[k]) * univariate_normals[i];
        }
    }

    for (k=0; names[k]!=NULL; k++) {
        if (LALInferenceCheckVariableNonFixed(proposedParams, names[k]) ) {
            tmp = LALInferenceGetREAL8Variable(proposedParams, names[k]) + w[k];
            LALInferenceSetVariable(proposedParams, names[k], &tmp);
        }
    }

    return logPropRatio;
}

REAL8 LALInferenceDifferentialEvolutionNames(LALInferenceThreadState *thread,
                                             LALInferenceVariables *currentParams,
                                             LALInferenceVariables *proposedParams,
                                             const char **names) {
    size_t i, j, N, Ndim, nPts;
    LALInferenceVariableItem *item;
    LALInferenceVariables **dePts;
    LALInferenceVariables *ptI, *ptJ;
    REAL8 logPropRatio = 0.0;
    REAL8 scale, x;

    LALInferenceCopyVariables(currentParams, proposedParams);

    gsl_rng *rng = thread->GSLrandom;

    if (names == NULL) {
        N = LALInferenceGetVariableDimension(currentParams) + 1; /* More names than we need. */
        names = alloca(N * sizeof(char *)); /* Hope we have alloca---saves
                                               having to deallocate after
                                               proposal. */

        item = currentParams->head;
        i = 0;
        while (item != NULL) {
            if (LALInferenceCheckVariableNonFixed(currentParams, item->name) && item->type==LALINFERENCE_REAL8_t ) {
                names[i] = item->name;
                i++;
            }

            item = item->next;
        }
        names[i]=NULL; /* Terminate */
    }


    Ndim = 0;
    for (Ndim=0, i=0; names[i] != NULL; i++ ) {
        if (LALInferenceCheckVariableNonFixed(proposedParams, names[i]))
            Ndim++;
    }

    dePts = thread->differentialPoints;
    nPts = thread->differentialPointsLength;

    if (dePts == NULL || nPts <= 1)
        return logPropRatio; /* Quit now, since we don't have any points to use. */

    i = gsl_rng_uniform_int(rng, nPts);
    do {
        j = gsl_rng_uniform_int(rng, nPts);
    } while (j == i);

    ptI = dePts[i];
    ptJ = dePts[j];

    const REAL8 modeHoppingFrac = 0.5;
    /* Some fraction of the time, we do a "mode hopping" jump,
       where we jump exactly along the difference vector. */
    if (gsl_rng_uniform(rng) < modeHoppingFrac) {
        scale = 1.0;
    } else {
        /* Otherwise scale is chosen uniform in log between 0.1 and 10 times the
        desired jump size. */
        scale = 2.38/sqrt(Ndim) * exp(log(0.1) + log(100.0) * gsl_rng_uniform(rng));
    }

    for (i = 0; names[i] != NULL; i++) {
        if (!LALInferenceCheckVariableNonFixed(proposedParams, names[i]) ||
            !LALInferenceCheckVariable(ptJ, names[i]) ||
            !LALInferenceCheckVariable(ptI, names[i])) {
        /* Ignore variable if it's not in each of the params. */
        } else {
            x = LALInferenceGetREAL8Variable(proposedParams, names[i]);
            x += scale * LALInferenceGetREAL8Variable(ptJ, names[i]);
            x -= scale * LALInferenceGetREAL8Variable(ptI, names[i]);
            LALInferenceSetVariable(proposedParams, names[i], &x);
        }
    }

    return logPropRatio;
}

REAL8 LALInferenceDifferentialEvolutionIntrinsic(LALInferenceThreadState *thread,
                                                 LALInferenceVariables *currentParams,
                                                 LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    const char *names[] = {"chirpmass", "q", "eta", "m1", "m2", "a_spin1", "a_spin2",
                           "tilt_spin1", "tilt_spin2", "phi12",  NULL};

    logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}

REAL8 LALInferenceDifferentialEvolutionExtrinsic(LALInferenceThreadState *thread,
                                                 LALInferenceVariables *currentParams,
                                                 LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;
    INT4 margtime, margphi;

    LALInferenceVariables *args = thread->proposalArgs;

    const char *names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                           "phase", "time", "costheta_jn", "cosalpha", "t0", "theta", NULL};

    const char *marg_time_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                     "phase", "costheta_jn","cosalpha", "t0", "theta", NULL};

    const char *marg_phase_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                      "time", "costheta_jn", "cosalpha","t0", "theta", NULL};

    const char *marg_time_phase_names[] = {"rightascension", "declination", "polarisation", "distance", "logdistance",
                                           "costheta_jn", "cosalpha", "t0", "theta", NULL};

    margtime = LALInferenceGetINT4Variable(args, "marg_time");
    margphi = LALInferenceGetINT4Variable(args, "marg_phi");

    if (margtime && margphi)
        logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, marg_time_phase_names);
    else if (margtime)
        logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, marg_time_names);
    else if (margphi)
        logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, marg_phase_names);
    else
        logPropRatio = LALInferenceDifferentialEvolutionNames(thread, currentParams, proposedParams, names);

    return logPropRatio;
}

static REAL8 draw_distance(LALInferenceThreadState *thread) {
    REAL8 dmin, dmax, x;

    LALInferenceGetMinMaxPrior(thread->priorArgs, "distance", &dmin, &dmax);

    x = gsl_rng_uniform(thread->GSLrandom);

    return cbrt(x*(dmax*dmax*dmax - dmin*dmin*dmin) + dmin*dmin*dmin);
}

static REAL8 draw_logdistance(LALInferenceThreadState *thread) {
    REAL8 logdmin, logdmax;

    LALInferenceGetMinMaxPrior(thread->priorArgs, "logdistance", &logdmin, &logdmax);

    REAL8 dmin=exp(logdmin);
    REAL8 dmax=exp(logdmax);

    REAL8 x = gsl_rng_uniform(thread->GSLrandom);

    return log(cbrt(x*(dmax*dmax*dmax - dmin*dmin*dmin) + dmin*dmin*dmin));
}

static REAL8 draw_colatitude(LALInferenceThreadState *thread, const char *name) {
    REAL8 min, max, x;

    LALInferenceGetMinMaxPrior(thread->priorArgs, name, &min, &max);

    x = gsl_rng_uniform(thread->GSLrandom);

    return acos(cos(min) - x*(cos(min) - cos(max)));
}

static REAL8 draw_dec(LALInferenceThreadState *thread) {
    REAL8 min, max, x;

    LALInferenceGetMinMaxPrior(thread->priorArgs, "declination", &min, &max);

    x = gsl_rng_uniform(thread->GSLrandom);

    return asin(x*(sin(max) - sin(min)) + sin(min));
}

static REAL8 draw_flat(LALInferenceThreadState *thread, const char *name) {
    REAL8 min, max, x;

    LALInferenceGetMinMaxPrior(thread->priorArgs, name, &min, &max);

    x = gsl_rng_uniform(thread->GSLrandom);

    return min + x*(max - min);
}

static REAL8 draw_chirp(LALInferenceThreadState *thread) {
    REAL8 min, max, delta;
    REAL8 mMin56, mMax56, u;

    LALInferenceGetMinMaxPrior(thread->priorArgs, "chirpmass", &min, &max);

    mMin56 = pow(min, 5.0/6.0);
    mMax56 = pow(max, 5.0/6.0);

    delta = 1.0/mMin56 - 1.0/mMax56;

    u = delta*gsl_rng_uniform(thread->GSLrandom);

    return pow(1.0/(1.0/mMin56 - u), 6.0/5.0);
}

static REAL8 approxLogPrior(LALInferenceVariables *params) {
    REAL8 logP = 0.0;

    REAL8 Mc = *(REAL8 *)LALInferenceGetVariable(params, "chirpmass");
    logP += -11.0/6.0*log(Mc);

    /* Flat in time, ra, psi, phi. */

    if(LALInferenceCheckVariable(params,"logdistance"))
      logP += 3.0* *(REAL8 *)LALInferenceGetVariable(params,"logdistance");
    else if(LALInferenceCheckVariable(params,"distance"))
      logP += 2.0*log(*(REAL8 *)LALInferenceGetVariable(params,"distance"));

    REAL8 dec = *(REAL8 *)LALInferenceGetVariable(params, "declination");
    logP += log(cos(dec));

    return logP;
}

REAL8 LALInferenceDrawApproxPrior(LALInferenceThreadState *thread,
                                  LALInferenceVariables *currentParams,
                                  LALInferenceVariables *proposedParams) {
    REAL8 tmp = 0.0;
    INT4 analytic_test, i;
    REAL8 logBackwardJump;
    REAL8 logPropRatio;
    LALInferenceVariableItem *ptr;

    LALInferenceCopyVariables(currentParams, proposedParams);

    const char *flat_params[] = {"q", "eta", "time", "phase", "polarisation",
                                 "rightascension", "costheta_jn", "phi_jl",
                                 "phi12", "a_spin1", "a_spin2", NULL};

    LALInferenceVariables *args = thread->proposalArgs;

    analytic_test = LALInferenceGetINT4Variable(args, "analytical_test");

    if (analytic_test) {
        ptr = currentParams->head;
        while (ptr!=NULL) {
            if (LALInferenceCheckVariableNonFixed(currentParams, ptr->name)) {
                tmp = draw_flat(thread, ptr->name);
                LALInferenceSetVariable(proposedParams, ptr->name, &tmp);
            }
            ptr=ptr->next;
        }
    } else {
        logBackwardJump = approxLogPrior(currentParams);

        for (i = 0; flat_params[i] != NULL; i++) {
            if (LALInferenceCheckVariableNonFixed(proposedParams, flat_params[i])) {
                REAL8 val = draw_flat(thread, flat_params[i]);
                LALInferenceSetVariable(proposedParams, flat_params[i], &val);
            }
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "chirpmass")) {
            REAL8 Mc = draw_chirp(thread);
            LALInferenceSetVariable(proposedParams, "chirpmass", &Mc);
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "logdistance")) {
            REAL8 logdist = draw_logdistance(thread);
            LALInferenceSetVariable(proposedParams, "logdistance", &logdist);
        } else if (LALInferenceCheckVariableNonFixed(proposedParams, "distance")) {
            REAL8 dist = draw_distance(thread);
            LALInferenceSetVariable(proposedParams, "distance", &dist);
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "declination")) {
            REAL8 dec = draw_dec(thread);
            LALInferenceSetVariable(proposedParams, "declination", &dec);
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "tilt_spin1")) {
            REAL8 tilt1 = draw_colatitude(thread, "tilt_spin1");
            LALInferenceSetVariable(proposedParams, "tilt_spin1", &tilt1);
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "tilt_spin2")) {
            REAL8 tilt2 = draw_colatitude(thread, "tilt_spin2");
            LALInferenceSetVariable(proposedParams, "tilt_spin2", &tilt2);
        }

        if (LALInferenceCheckVariableNonFixed(proposedParams, "psdscale")) {
            REAL8 x, min, max;
            INT4 j;

            min=0.10;
            max=10.0;

            gsl_matrix *eta = LALInferenceGetgslMatrixVariable(proposedParams, "psdscale");

            for(i=0; i<(INT8)eta->size1; i++) {
                for(j=0; j<(INT8)eta->size2; j++) {
                    x = min + gsl_rng_uniform(thread->GSLrandom) * (max - min);
                    gsl_matrix_set(eta, i, j, x);
                }
            }
        }//end if(psdscale)
    }

    if (analytic_test) {
        /* Flat in every variable means uniform jump probability. */
        logPropRatio = 0.0;
    } else {
        logPropRatio = logBackwardJump - approxLogPrior(proposedParams);
    }

    return logPropRatio;
}

static void cross_product(REAL8 x[3], const REAL8 y[3], const REAL8 z[3]) {
    x[0] = y[1]*z[2]-y[2]*z[1];
    x[1] = y[2]*z[0]-y[0]*z[2];
    x[2] = y[0]*z[1]-y[1]*z[0];
}

static REAL8 norm(const REAL8 x[3]) {
    return sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
}

static void unit_vector(REAL8 v[3], const REAL8 w[3]) {
    REAL8 n = norm(w);

    if (n == 0.0) {
        XLALError("unit_vector", __FILE__, __LINE__, XLAL_FAILURE);
        exit(1);
    } else {
        v[0] = w[0] / n;
        v[1] = w[1] / n;
        v[2] = w[2] / n;
    }
}

static REAL8 dot(const REAL8 v[3], const REAL8 w[3]) {
    return v[0]*w[0] + v[1]*w[1] + v[2]*w[2];
}

static void project_along(REAL8 vproj[3], const REAL8 v[3], const REAL8 w[3]) {
    REAL8 what[3];
    REAL8 vdotw;

    unit_vector(what, w);
    vdotw = dot(v, w);

    vproj[0] = what[0]*vdotw;
    vproj[1] = what[1]*vdotw;
    vproj[2] = what[2]*vdotw;
}

static void vsub(REAL8 diff[3], const REAL8 w[3], const REAL8 v[3]) {
    diff[0] = w[0] - v[0];
    diff[1] = w[1] - v[1];
    diff[2] = w[2] - v[2];
}

static void reflect_plane(REAL8 pref[3], const REAL8 p[3],
                          const REAL8 x[3], const REAL8 y[3], const REAL8 z[3]) {
    REAL8 n[3], nhat[3], xy[3], xz[3], pn[3], pnperp[3];

    vsub(xy, y, x);
    vsub(xz, z, x);

    cross_product(n, xy, xz);
    unit_vector(nhat, n);

    project_along(pn, p, nhat);
    vsub(pnperp, p, pn);

    vsub(pref, pnperp, pn);
}

static void sph_to_cart(REAL8 cart[3], const REAL8 lat, const REAL8 longi) {
    cart[0] = cos(longi)*cos(lat);
    cart[1] = sin(longi)*cos(lat);
    cart[2] = sin(lat);
}

static void cart_to_sph(const REAL8 cart[3], REAL8 *lat, REAL8 *longi) {
    *longi = atan2(cart[1], cart[0]);
    *lat = asin(cart[2] / sqrt(cart[0]*cart[0] + cart[1]*cart[1] + cart[2]*cart[2]));
}

static void reflected_position_and_time(LALInferenceThreadState *thread, const REAL8 ra, const REAL8 dec,
                                        const REAL8 oldTime, REAL8 *newRA, REAL8 *newDec, REAL8 *newTime) {
    LALStatus status;
    memset(&status, 0, sizeof(status));
    SkyPosition currentEqu, currentGeo, newEqu, newGeo;
    LALDetector *detectors;
    LIGOTimeGPS *epoch;
    REAL8 x[3], y[3], z[3];
    REAL8 currentLoc[3], newLoc[3];
    REAL8 newGeoLat, newGeoLongi;
    REAL8 oldDt, newDt;
    LALDetector xD, yD, zD;

    currentEqu.latitude = dec;
    currentEqu.longitude = ra;
    currentEqu.system = COORDINATESYSTEM_EQUATORIAL;
    currentGeo.system = COORDINATESYSTEM_GEOGRAPHIC;

    LALInferenceVariables *args = thread->proposalArgs;

    epoch = (LIGOTimeGPS *)LALInferenceGetVariable(args, "epoch");
    detectors = *(LALDetector **)LALInferenceGetVariable(args, "detectors");

    LALEquatorialToGeographic(&status, &currentGeo, &currentEqu, epoch);

    /* This function should only be called when we know that we have
     three detectors, or the following will crash. */
    xD = detectors[0];
    memcpy(x, xD.location, 3*sizeof(REAL8));

    INT4 det = 1;
    yD = detectors[det];
    while (same_detector_location(&yD, &xD)) {
        det++;
        yD = detectors[det];
    }
    memcpy(y, yD.location, 3*sizeof(REAL8));
    det++;

    zD = detectors[det];
    while (same_detector_location(&zD, &yD) || same_detector_location(&zD, &xD)) {
        det++;
        zD = detectors[det];
    }
    memcpy(z, zD.location, 3*sizeof(REAL8));

    sph_to_cart(currentLoc, currentGeo.latitude, currentGeo.longitude);

    reflect_plane(newLoc, currentLoc, x, y, z);

    cart_to_sph(newLoc, &newGeoLat, &newGeoLongi);

    newGeo.latitude = newGeoLat;
    newGeo.longitude = newGeoLongi;
    newGeo.system = COORDINATESYSTEM_GEOGRAPHIC;
    newEqu.system = COORDINATESYSTEM_EQUATORIAL;
    LALGeographicToEquatorial(&status, &newEqu, &newGeo, epoch);

    oldDt = XLALTimeDelayFromEarthCenter(detectors[0].location, currentEqu.longitude,
                                         currentEqu.latitude, epoch);
    newDt = XLALTimeDelayFromEarthCenter(detectors[0].location, newEqu.longitude,
                                         newEqu.latitude, epoch);

    *newRA = newEqu.longitude;
    *newDec = newEqu.latitude;
    *newTime = oldTime + oldDt - newDt;
}

static REAL8 evaluate_morlet_proposal(LALInferenceThreadState *thread,
                                      LALInferenceVariables *proposedParams,
                                      INT4 ifo, INT4 k) {
    REAL8 prior = 0.0;
    REAL8 component_min,component_max;
    REAL8 A, f, Q, Anorm;
    gsl_matrix *glitch_f, *glitch_Q, *glitch_A;

    component_min = LALInferenceGetREAL8Variable(thread->priorArgs,"morlet_f0_prior_min");
    component_max = LALInferenceGetREAL8Variable(thread->priorArgs,"morlet_f0_prior_max");
    prior -= log(component_max - component_min);

    component_min = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_Q_prior_min");
    component_max = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_Q_prior_max");
    prior -= log(component_max - component_min);

    component_min = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_t0_prior_min");
    component_max = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_t0_prior_max");
    prior -= log(component_max - component_min);

    component_min = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_phi_prior_min");
    component_max = LALInferenceGetREAL8Variable(thread->priorArgs, "morlet_phi_prior_max");
    prior -= log(component_max - component_min);

    //"Malmquist" prior on A
    glitch_f = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
    glitch_Q = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
    glitch_A = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");

    A = gsl_matrix_get(glitch_A, ifo, k);
    Q = gsl_matrix_get(glitch_Q, ifo, k);
    f = gsl_matrix_get(glitch_f, ifo, k);

    Anorm = LALInferenceGetREAL8Variable(thread->priorArgs, "glitch_norm");

    prior += logGlitchAmplitudeDensity(A*Anorm, Q, f);

    return prior;
}


static REAL8 glitchAmplitudeDraw(REAL8 Q, REAL8 f, gsl_rng *r) {
    REAL8 SNR;
    REAL8 PIterm = 0.5*LAL_2_SQRTPI*LAL_SQRT1_2;
    REAL8 SNRPEAK = 5.0;

    INT4 k=0;
    REAL8 den=0.0, alpha=1.0;
    REAL8 max= 1.0/(SNRPEAK*LAL_E);;

    // x/a^2 exp(-x/a) prior on SNR. Peaks at x = a. Good choice is a=5

    // rejection sample. Envelope function on runs out to ten times the peak
    // don't even bother putting in this minute correction to the normalization
    // (it is a 5e-4 correction).
    do {
        SNR = 20.0 * SNRPEAK * gsl_rng_uniform(r);

        den = SNR/(SNRPEAK*SNRPEAK) * exp(-SNR/SNRPEAK);

        den /= max;

        alpha = gsl_rng_uniform(r);

        k++;
    } while (alpha > den);

    return SNR/sqrt((PIterm*Q/f));
}

REAL8 LALInferenceSkyRingProposal(LALInferenceThreadState *thread,
                                  LALInferenceVariables *currentParams,
                                  LALInferenceVariables *proposedParams) {
    INT4 i, j, l;
    INT4 ifo, nifo, timeflag=0;
    REAL8 logPropRatio = 0.0;
    REAL8 dL, ra, dec, psi;
    REAL8 baryTime, gmst;
    REAL8 newRA, newDec, newTime, newPsi, newDL;
    REAL8 intpart, decpart;
    REAL8 omega, cosomega, sinomega, c1momega;
    REAL8 IFO1[3], IFO2[3];
    REAL8 IFOX[3], k[3];
    REAL8 normalize;
    REAL8 pForward, pReverse;
    REAL8 Fx,Fy;
    REAL8 Fp,Fc;
    REAL8 n[3];
    REAL8 kp[3];

    LALInferenceCopyVariables(currentParams, proposedParams);

    LIGOTimeGPS GPSlal, *epoch;
    LALDetector *detectors;
    gsl_matrix *IFO;

    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng *rng = thread->GSLrandom;

    epoch = (LIGOTimeGPS *)LALInferenceGetVariable(args, "epoch");
    detectors = *(LALDetector **)LALInferenceGetVariable(args, "detectors");

    dL = exp(LALInferenceGetREAL8Variable(proposedParams, "logdistance"));

    ra = LALInferenceGetREAL8Variable(proposedParams, "rightascension");
    dec = LALInferenceGetREAL8Variable(proposedParams, "declination");
    psi = LALInferenceGetREAL8Variable(proposedParams, "polarisation");

    if (LALInferenceCheckVariable(proposedParams, "time")){
        baryTime = LALInferenceGetREAL8Variable(proposedParams, "time");
        timeflag = 1;
    } else {
        baryTime = XLALGPSGetREAL8(epoch);
    }

    XLALGPSSetREAL8(&GPSlal, baryTime);
    gmst = XLALGreenwichMeanSiderealTime(&GPSlal);

    //remap gmst back to [0:2pi]
    gmst /= LAL_TWOPI;
    intpart = (INT4)gmst;
    decpart = gmst - (REAL8)intpart;
    gmst = decpart*LAL_TWOPI;
    gmst = gmst < 0. ? gmst + LAL_TWOPI : gmst;

    /*
    line-of-sight vector
    */
    k[0] = cos(gmst-ra) * cos(dec);
    k[1] =-sin(gmst-ra) * cos(dec);
    k[2] = sin(dec);

    /*
    Store location for each detector
    */
    nifo = LALInferenceGetINT4Variable(args, "nDet");

    IFO = gsl_matrix_alloc(nifo, 3);

    for(ifo=0; ifo<nifo; ifo++) {
        memcpy(IFOX, detectors[ifo].location, 3*sizeof(REAL8));
        for (i=0; i<3; i++)
            gsl_matrix_set(IFO, ifo, i, IFOX[i]);
    }

    /*
    Randomly select two detectors from the network
    -this assumes there are no co-located detectors
    */
    i = j = 0;
    while (i==j) {
        i=gsl_rng_uniform_int(rng, nifo);
        j=gsl_rng_uniform_int(rng, nifo);
    }

    for(l=0; l<3; l++) {
        IFO1[l] = gsl_matrix_get(IFO, i, l);
        IFO2[l] = gsl_matrix_get(IFO, j, l);
    }

    /*
    detector axis
    */
    normalize=0.0;
    for(i=0; i<3; i++) {
        n[i] = IFO1[i] - IFO2[i];
        normalize += n[i] * n[i];
    }
    normalize = 1./sqrt(normalize);
    for(i=0; i<3; i++)
        n[i] *= normalize;

    /*
    rotation angle
    */
    omega = LAL_TWOPI * gsl_rng_uniform(rng);
    cosomega = cos(omega);
    sinomega = sin(omega);
    c1momega = 1.0 - cosomega;

    /*
    rotate k' = Rk
    */
    kp[0] = (c1momega*n[0]*n[0] + cosomega) * k[0]
            + (c1momega*n[0]*n[1] - sinomega*n[2]) * k[1]
            + (c1momega*n[0]*n[2] + sinomega*n[1]) * k[2];
    kp[1] = (c1momega*n[0]*n[1] + sinomega*n[2]) * k[0]
            + (c1momega*n[1]*n[1] + cosomega) * k[1]
            + (c1momega*n[1]*n[2] - sinomega*n[0]) * k[2];
    kp[2] = (c1momega*n[0]*n[2] - sinomega*n[1]) * k[0]
            + (c1momega*n[1]*n[2] + sinomega*n[0]) * k[1]
            + (c1momega*n[2]*n[2] + cosomega) * k[2];

    /*
    convert k' back to ra' and dec'
    */
    newDec = asin(kp[2]);
    newRA = atan2(kp[1], kp[0]) + gmst;
    if (newRA < 0.0)
        newRA += LAL_TWOPI;
    else if (newRA >= LAL_TWOPI)
        newRA -= LAL_TWOPI;
    /*
    compute new geocenter time using
    fixed arrival time at IFO1 (arbitrary)
    */
    REAL8 tx; //old time shift = k * n
    REAL8 ty; //new time shift = k'* n
    tx=ty=0;
    for(i=0; i<3; i++) {
        tx += -IFO1[i]*k[i] /LAL_C_SI;
        ty += -IFO1[i]*kp[i]/LAL_C_SI;
    }
    newTime = tx + baryTime - ty;

    XLALGPSSetREAL8(&GPSlal, newTime);
    REAL8 newGmst = XLALGreenwichMeanSiderealTime(&GPSlal);

    /*
    draw new polarisation angle uniformally
    for now
    MARK: Need to be smarter about psi in sky-ring jump
    */
    newPsi = LAL_PI * gsl_rng_uniform(rng);

    /*
    compute new luminosity distance,
    maintaining F+^2 + Fx^2 across the network
    */
    Fx=0;
    Fy=0;

    for (i=0; i<nifo; i++) {
        XLALComputeDetAMResponse(&Fp, &Fc, (const REAL4(*)[3])detectors[i].response, ra, dec, psi, gmst);
        Fx += Fp*Fp+Fc*Fc;

        XLALComputeDetAMResponse(&Fp, &Fc, (const REAL4(*)[3])detectors[i].response, newRA, newDec, newPsi, newGmst);
        Fy += Fp*Fp+Fc*Fc;
    }
    newDL = dL*sqrt(Fy/Fx);

    /*
    update new parameters and exit.  woo!
    */
    REAL8 logNewDL = log(newDL);
    LALInferenceSetVariable(proposedParams, "logdistance", &logNewDL);

    LALInferenceSetVariable(proposedParams, "polarisation", &newPsi);
    LALInferenceSetVariable(proposedParams, "rightascension", &newRA);
    LALInferenceSetVariable(proposedParams, "declination", &newDec);
    if (timeflag)
        LALInferenceSetVariable(proposedParams, "time", &newTime);

    pForward = cos(newDec);
    pReverse = cos(dec);

    gsl_matrix_free(IFO);

    logPropRatio = log(pReverse/pForward);

    return logPropRatio;
}

REAL8 LALInferenceSkyReflectDetPlane(LALInferenceThreadState *thread,
                                     LALInferenceVariables *currentParams,
                                     LALInferenceVariables *proposedParams) {
    INT4 timeflag=0;
    REAL8 ra, dec, baryTime;
    REAL8 newRA, newDec, newTime;
    REAL8 nRA, nDec, nTime;
    REAL8 refRA, refDec, refTime;
    REAL8 nRefRA, nRefDec, nRefTime;
    REAL8 pForward, pReverse;
    REAL8 logPropRatio = 0.0;
    INT4 nUniqueDet;
    LIGOTimeGPS *epoch;

    LALInferenceCopyVariables(currentParams, proposedParams);

    /* Find the number of distinct-position detectors. */
    /* Exit with same parameters (with a warning the first time) if
    there are not three detectors. */
    static INT4 warningDelivered = 0;

    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng *rng = thread->GSLrandom;

    epoch = (LIGOTimeGPS *)LALInferenceGetVariable(args, "epoch");
    nUniqueDet = LALInferenceGetINT4Variable(args, "nUniqueDet");

    if (nUniqueDet != 3) {
        if (!warningDelivered) {
            fprintf(stderr, "WARNING: trying to reflect through the decector plane with %d\n", nUniqueDet);
            fprintf(stderr, "WARNING: geometrically independent locations,\n");
            fprintf(stderr, "WARNING: but this proposal should only be used with exactly 3 independent detectors.\n");
            fprintf(stderr, "WARNING: %s, line %d\n", __FILE__, __LINE__);
            warningDelivered = 1;
        }

        return logPropRatio;
    }

    ra = LALInferenceGetREAL8Variable(proposedParams, "rightascension");
    dec = LALInferenceGetREAL8Variable(proposedParams, "declination");

    if (LALInferenceCheckVariable(proposedParams, "time")){
        baryTime = LALInferenceGetREAL8Variable(proposedParams, "time");
        timeflag=1;
    } else {
        baryTime = XLALGPSGetREAL8(epoch);
    }

    reflected_position_and_time(thread, ra, dec, baryTime, &newRA, &newDec, &newTime);

    /* Unit normal deviates, used to "fuzz" the state. */
    const REAL8 epsTime = 6e-6; /* 1e-1 / (16 kHz) */
    const REAL8 epsAngle = 3e-4; /* epsTime*c/R_Earth */

    nRA = gsl_ran_ugaussian(rng);
    nDec = gsl_ran_ugaussian(rng);
    nTime = gsl_ran_ugaussian(rng);

    newRA += epsAngle*nRA;
    newDec += epsAngle*nDec;
    newTime += epsTime*nTime;

    /* And the doubly-reflected position (near the original, but not
    exactly due to the fuzzing). */
    reflected_position_and_time(thread, newRA, newDec, newTime, &refRA, &refDec, &refTime);

    /* The Gaussian increments required to shift us back to the original
    position from the doubly-reflected position. */
    nRefRA = (ra - refRA)/epsAngle;
    nRefDec = (dec - refDec)/epsAngle;
    nRefTime = (baryTime - refTime)/epsTime;

    pForward = gsl_ran_ugaussian_pdf(nRA) * gsl_ran_ugaussian_pdf(nDec) * gsl_ran_ugaussian_pdf(nTime);
    pReverse = gsl_ran_ugaussian_pdf(nRefRA) * gsl_ran_ugaussian_pdf(nRefDec) * gsl_ran_ugaussian_pdf(nRefTime);

    LALInferenceSetVariable(proposedParams, "rightascension", &newRA);
    LALInferenceSetVariable(proposedParams, "declination", &newDec);

    if (timeflag)
        LALInferenceSetVariable(proposedParams, "time", &newTime);

    logPropRatio = log(pReverse/pForward);

    return logPropRatio;
}

REAL8 LALInferencePSDFitJump(LALInferenceThreadState *thread,
                             LALInferenceVariables *currentParams,
                             LALInferenceVariables *proposedParams) {
    INT4 i,j;
    INT4 N, nifo;
    REAL8 draw=0.0;
    REAL8 logPropRatio = 0.0;
    REAL8Vector *var;
    gsl_matrix *ny;

    LALInferenceCopyVariables(currentParams, proposedParams);

    var = LALInferenceGetREAL8VectorVariable(thread->proposalArgs, "psdsigma");

    //Get current state of chain into workable form
    ny = LALInferenceGetgslMatrixVariable(proposedParams, "psdscale");

    //Get size of noise parameter array
    nifo = (INT4)ny->size1;
    N = (INT4)ny->size2;

    //perturb noise parameter
    for(i=0; i<nifo; i++) {
        for(j=0; j<N; j++) {
            draw = gsl_matrix_get(ny, i, j) + gsl_ran_ugaussian(thread->GSLrandom) * var->data[j];
            gsl_matrix_set(ny, i, j, draw);
        }
    }

    return logPropRatio;
}

static void UpdateWaveletSum(LALInferenceThreadState *thread,
                             LALInferenceVariables *proposedParams,
                             gsl_matrix *glitchFD, INT4 ifo, INT4 n, INT4 flag) {
    INT4 i=0;
    INT4 lower, upper;
    INT4 glitchLower, glitchUpper;
    REAL8FrequencySeries **asds, *asd = NULL;
    REAL8Vector *flows;
    REAL8 deltaT, Tobs, deltaF;
    REAL8 Q, Amp, t0, ph0, f0; //sine-Gaussian parameters
    REAL8 amparg, phiarg, Ai;//helpers for computing sineGaussian
    REAL8 gRe, gIm;         //real and imaginary parts of current glitch model
    REAL8 tau;
    gsl_matrix *glitch_f, *glitch_Q, *glitch_A;
    gsl_matrix *glitch_t, *glitch_p;
    REAL8TimeSeries **td_data;

    LALInferenceVariables *args = thread->proposalArgs;

    asds = *(REAL8FrequencySeries ***)LALInferenceGetVariable(args, "asds");
    flows = LALInferenceGetREAL8VectorVariable(args, "flows");
    td_data = *(REAL8TimeSeries ***)LALInferenceGetVariable(args, "td_data");

    /* get dataPtr pointing to correct IFO */
    asd = asds[ifo];

    deltaT = td_data[ifo]->deltaT;
    Tobs = (((REAL8)td_data[ifo]->data->length) * deltaT);
    deltaF = 1.0 / Tobs;

    lower = (INT4)ceil(flows->data[ifo] / deltaF);
    upper = (INT4)floor(flows->data[ifo] / deltaF);

    glitch_f = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
    glitch_Q = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
    glitch_A = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");
    glitch_t = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_t0");
    glitch_p = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_phi");

    Q = gsl_matrix_get(glitch_Q, ifo, n);
    Amp = gsl_matrix_get(glitch_A, ifo, n);
    t0 = gsl_matrix_get(glitch_t, ifo, n);
    ph0 = gsl_matrix_get(glitch_p, ifo, n);
    f0 = gsl_matrix_get(glitch_f, ifo, n);

    //6 x decay time of sine Gaussian (truncate how much glitch we compute)
    tau = Q/LAL_TWOPI/f0;
    glitchLower = (INT4)floor((f0 - 1./tau)/deltaF);
    glitchUpper = (INT4)floor((f0 + 1./tau)/deltaF);

    //set glitch model to zero
    if (flag==0) {
        for(i=lower; i<=upper; i++) {
            gsl_matrix_set(glitchFD, ifo, 2*i, 0.0);
            gsl_matrix_set(glitchFD, ifo, 2*i+1, 0.0);
        }
    }

    for (i=glitchLower; i<glitchUpper; i++) {
        if (i>=lower && i<=upper) {
            gRe = gsl_matrix_get(glitchFD, ifo, 2*i);
            gIm = gsl_matrix_get(glitchFD, ifo, 2*i+1);
            amparg = ((REAL8)i*deltaF - f0)*LAL_PI*tau;
            phiarg = LAL_PI*(REAL8)i + ph0 - LAL_TWOPI*(REAL8)i*deltaF*(t0-Tobs/2.);//TODO: SIMPLIFY PHASE FOR SINEGAUSSIAN
            Ai = Amp*tau*0.5*sqrt(LAL_PI)*exp(-amparg*amparg)*asd->data->data[i]/sqrt(Tobs);

            switch(flag) {
                // Remove wavelet from model
                case -1:
                    gRe -= Ai*cos(phiarg);
                    gIm -= Ai*sin(phiarg);
                    break;
                // Add wavelet to model
                case  1:
                    gRe += Ai*cos(phiarg);
                    gIm += Ai*sin(phiarg);
                    break;
                // Replace model with wavelet
                case 0:
                    gRe = Ai*cos(phiarg);
                    gIm = Ai*sin(phiarg);
                    break;
                //Do nothing
                default:
                    break;
            }//end switch

            //update glitch model
            gsl_matrix_set(glitchFD, ifo, 2*i, gRe);
            gsl_matrix_set(glitchFD, ifo, 2*i+1, gIm);

        }//end upper/lower check
    }//end loop over glitch samples
}

static void phase_blind_time_shift(REAL8 *corr, REAL8 *corrf, COMPLEX16Vector *data1,
                                   COMPLEX16Vector *data2, INT4 ifo, LALInferenceVariables *args) {
    INT4 i, N, N2;
    INT4 lower, upper;
    REAL8 deltaF, deltaT;
    REAL8FrequencySeries **psds;
    REAL8FrequencySeries *psd;
    COMPLEX16FrequencySeries *corrFD, *corrfFD;
    REAL8TimeSeries *corrTD, *corrfTD;
    REAL8TimeSeries **td_data;
    COMPLEX16FrequencySeries **fd_data;
    REAL8FFTPlan **plans;
    REAL8Vector *flows, *fhighs;

    psds = *(REAL8FrequencySeries ***)LALInferenceGetVariable(args, "psds");
    flows = LALInferenceGetREAL8VectorVariable(args, "flows");
    fhighs = LALInferenceGetREAL8VectorVariable(args, "fhighs");

    td_data = *(REAL8TimeSeries ***)LALInferenceGetVariable(args, "td_data");
    fd_data = *(COMPLEX16FrequencySeries ***)LALInferenceGetVariable(args, "fd_data");

    plans = *(REAL8FFTPlan ***)LALInferenceGetVariable(args, "f2t_plans");

    /* get dataPtr pointing to correct IFO */
    psd = psds[ifo];

    N  = td_data[ifo]->data->length;   // Number of data points
    N2 = fd_data[ifo]->data->length-1; // 1/2 number of data points (plus 1)

    deltaF = fd_data[ifo]->deltaF;
    deltaT = td_data[ifo]->deltaT;

    lower  = (INT4)ceil(flows->data[ifo]  / deltaF);
    upper  = (INT4)floor(fhighs->data[ifo] / deltaF);

    corrFD  = XLALCreateCOMPLEX16FrequencySeries("cf1", &(fd_data[ifo]->epoch), 0.0, deltaF, &lalDimensionlessUnit, N2+1);
    corrfFD = XLALCreateCOMPLEX16FrequencySeries("cf2", &(fd_data[ifo]->epoch), 0.0, deltaF, &lalDimensionlessUnit, N2+1);

    corrTD  = XLALCreateREAL8TimeSeries("ct1", &(td_data[ifo]->epoch), 0.0, deltaT, &lalDimensionlessUnit, N);
    corrfTD = XLALCreateREAL8TimeSeries("ct2", &(td_data[ifo]->epoch), 0.0, deltaT, &lalDimensionlessUnit, N);

    //convolution of signal & template
    for (i=0; i < N2; i++) {
        corrFD->data->data[i]  = crect(0.0,0.0);
        corrfFD->data->data[i] = crect(0.0,0.0);

        if(i>lower && i<upper) {
            corrFD->data->data[i] = crect( ( creal(data1->data[i])*creal(data2->data[i]) + cimag(data1->data[i])*cimag(data2->data[i])) / psd->data->data[i],
                                           ( cimag(data1->data[i])*creal(data2->data[i]) - creal(data1->data[i])*cimag(data2->data[i])) / psd->data->data[i] );
            corrfFD->data->data[i] = crect( ( creal(data1->data[i])*cimag(data2->data[i]) - cimag(data1->data[i])*creal(data2->data[i])) / psd->data->data[i],
                                            ( cimag(data1->data[i])*cimag(data2->data[i]) + creal(data1->data[i])*creal(data2->data[i])) / psd->data->data[i] );
        }
    }

    //invFFT convolutions to find time offset
    XLALREAL8FreqTimeFFT(corrTD, corrFD, plans[ifo]);
    XLALREAL8FreqTimeFFT(corrfTD, corrfFD, plans[ifo]);

    for (i=0; i < N; i++) {
        corr[i]  = corrTD->data->data[i];
        corrf[i] = corrfTD->data->data[i];
    }

    XLALDestroyREAL8TimeSeries(corrTD);
    XLALDestroyREAL8TimeSeries(corrfTD);
    XLALDestroyCOMPLEX16FrequencySeries(corrFD);
    XLALDestroyCOMPLEX16FrequencySeries(corrfFD);
}

static void MaximizeGlitchParameters(LALInferenceThreadState *thread,
                                     LALInferenceVariables *currentParams,
                                     INT4 ifo, INT4 n)
{
    INT4 i, imax, N;
    INT4 lower, upper;
    REAL8 deltaT, Tobs, deltaF, sqTwoDeltaToverN;
    REAL8 Amp, t0, ph0;
    REAL8 rho=0.0;
    REAL8 hRe, hIm;
    REAL8 gRe, gIm;
    REAL8 dPhase, dTime;
    REAL8 max;
    REAL8 *corr, *AC, *AF;
    REAL8FrequencySeries **psds;
    REAL8Vector *flows, *fhighs, *Sn;
    INT4Vector *gsize;
    COMPLEX16Sequence *s, *h, *r;
    gsl_matrix *glitchFD, *glitch_A, *glitch_t, *glitch_p, *hmatrix;
    REAL8TimeSeries **td_data;
    COMPLEX16FrequencySeries **fd_data;

    LALInferenceVariables *args = thread->proposalArgs;

    INT4 nDet = LALInferenceGetINT4Variable(args, "nDet");
    psds = *(REAL8FrequencySeries ***)LALInferenceGetVariable(args, "psds");
    flows = LALInferenceGetREAL8VectorVariable(args, "flows");
    fhighs = LALInferenceGetREAL8VectorVariable(args, "fhighs");

    td_data = XLALCalloc(nDet, sizeof(REAL8TimeSeries *));
    fd_data = XLALCalloc(nDet, sizeof(COMPLEX16FrequencySeries *));

    N = td_data[ifo]->data->length;
    deltaT = td_data[ifo]->deltaT;
    Tobs = (REAL8)(deltaT*N);
    sqTwoDeltaToverN = sqrt(2.0 * deltaT / ((REAL8) N) );

    deltaF = 1.0 / (((REAL8)N) * deltaT);
    lower = (INT4)ceil(flows->data[ifo] / deltaF);
    upper = (INT4)floor(fhighs->data[ifo] / deltaF);

    s = fd_data[ifo]->data;
    h = XLALCreateCOMPLEX16Vector(N/2);
    r = XLALCreateCOMPLEX16Vector(N/2);
    Sn = psds[ifo]->data;

    /* Get parameters for new wavelet */
    gsize = LALInferenceGetINT4VectorVariable(currentParams, "glitch_size");

    glitchFD = LALInferenceGetgslMatrixVariable(currentParams, "morlet_FD");
    glitch_A = LALInferenceGetgslMatrixVariable(currentParams, "morlet_Amp");
    glitch_t = LALInferenceGetgslMatrixVariable(currentParams, "morlet_t0");
    glitch_p = LALInferenceGetgslMatrixVariable(currentParams, "morlet_phi");

    /* sine-Gaussian parameters */
    Amp = gsl_matrix_get(glitch_A, ifo, n);
    t0 = gsl_matrix_get(glitch_t, ifo, n);
    ph0 = gsl_matrix_get(glitch_p, ifo, n);

    /* Make new wavelet */
    hmatrix = gsl_matrix_alloc(ifo+1, N);
    gsl_matrix_set_all(hmatrix, 0.0);

    UpdateWaveletSum(thread, currentParams, hmatrix, ifo, n, 1);

    /* Copy to appropriate template array*/
    for (i=0; i<N/2; i++) {
        hRe = 0.0;
        hIm = 0.0;
        gRe = 0.0;
        gIm = 0.0;
        r->data[i] = crect(0.0, 0.0);

        if(i>lower && i<upper) {
            hRe = sqTwoDeltaToverN * gsl_matrix_get(hmatrix, ifo, 2*i);
            hIm = sqTwoDeltaToverN * gsl_matrix_get(hmatrix, ifo, 2*i+1);
            h->data[i] = crect(hRe, hIm);
            //compute SNR of new wavelet
            rho += (hRe*hRe + hIm*hIm) / Sn->data[i];

            //form up residual while we're in here (w/out new template)
            if(gsize->data[ifo]>0) {
                gRe = gsl_matrix_get(glitchFD, ifo, 2*i);
                gIm = gsl_matrix_get(glitchFD, ifo, 2*i+1);
            }
            r->data[i] = crect(sqTwoDeltaToverN * (creal(s->data[i])/deltaT-gRe),
                               sqTwoDeltaToverN * (cimag(s->data[i])/deltaT-gIm));
        }
    }
    rho*=4.0;

    /* Compute correlation of data & template */
    corr = XLALMalloc(sizeof(REAL8) * N);
    AF = XLALMalloc(sizeof(REAL8) * N);
    AC = XLALMalloc(sizeof(REAL8) * N);

    for(i=0; i<N; i++)
        corr[i] = 0.0;

    /* Cross-correlate template & residual */
    phase_blind_time_shift(AC, AF, r, h, ifo, thread->proposalArgs);

    for(i=0; i<N; i++)
        corr[i] += sqrt(AC[i]*AC[i] + AF[i]*AF[i]);

    /* Find element where correlation is maximized */
    max = corr[0];
    imax = 0;
    for(i=1; i<N; i++) {
        if(corr[i] > max) {
            max  = corr[i];
            imax = i;
        }
    }
    max *= 4.0;

    /* Get phase shift at max correlation */
    dPhase = atan2(AF[imax], AC[imax]);

    /* Compute time shift needed for propsed template */
    if (imax < (N/2)-1)
        dTime = ((REAL8)imax/(REAL8)N) * Tobs;
    else
        dTime = (((REAL8)imax-(REAL8)N)/(REAL8)N) * Tobs;

    /* Shift template parameters accordingly */
    t0 += dTime;
    Amp *= 1.0;//dAmplitude;
    ph0 -= dPhase;

    /* Map time & phase back in range if necessary */
    if (ph0 < 0.0)
        ph0 += LAL_TWOPI;
    else if (ph0 > LAL_TWOPI)
        ph0 -= LAL_TWOPI;

    if (t0 < 0.0)
        t0 += Tobs;
    else if (t0 > Tobs)
        t0 -= Tobs;

    gsl_matrix_set(glitch_t, ifo, n, t0);
    gsl_matrix_set(glitch_A, ifo, n, Amp);
    gsl_matrix_set(glitch_p, ifo, n, ph0);

    gsl_matrix_free(hmatrix);

    XLALDestroyCOMPLEX16Vector(h);
    XLALDestroyCOMPLEX16Vector(r);

    XLALFree(corr);
    XLALFree(AF);
    XLALFree(AC);

}

static void MorletDiagonalFisherMatrix(REAL8Vector *params, REAL8Vector *sigmas) {
    REAL8 f0;
    REAL8 Q;
    REAL8 Amp;

    REAL8 sigma_t0;
    REAL8 sigma_f0;
    REAL8 sigma_Q;
    REAL8 sigma_Amp;
    REAL8 sigma_phi0;

    REAL8 SNR   = 0.0;
    REAL8 sqrt3 = 1.7320508;

    f0   = params->data[1];
    Q    = params->data[2];
    Amp  = params->data[3];

    SNR = Amp*sqrt(Q/(2.0*sqrt(LAL_TWOPI)*f0));

    // this caps the size of the proposed jumps
    if(SNR < 5.0) SNR = 5.0;

    sigma_t0   = 1.0/(LAL_TWOPI*f0*SNR);
    sigma_f0   = 2.0*f0/(Q*SNR);
    sigma_Q    = 2.0*Q/(sqrt3*SNR);
    sigma_Amp  = Amp/SNR;
    sigma_phi0 = 1.0/SNR;

    // Map diagonal Fisher elements to sigmas vector
    sigmas->data[0] = sigma_t0;
    sigmas->data[1] = sigma_f0;
    sigmas->data[2] = sigma_Q;
    sigmas->data[3] = sigma_Amp;
    sigmas->data[4] = sigma_phi0;
}

REAL8 LALInferenceGlitchMorletProposal(LALInferenceThreadState *thread,
                                       LALInferenceVariables *currentParams,
                                       LALInferenceVariables *proposedParams) {
    INT4 i, ifo;
    INT4 n;

    REAL8 logPropRatio = 0.0;
    REAL8 t0;
    REAL8 f0;
    REAL8 Q;
    REAL8 Amp;
    REAL8 phi0;

    REAL8 scale;

    REAL8 qyx;
    REAL8 qxy;
    REAL8 Anorm;

    LALInferenceCopyVariables(currentParams, proposedParams);

    gsl_matrix *glitchFD, *glitch_f, *glitch_Q, *glitch_A, *glitch_t, *glitch_p;

    gsl_rng *rng = thread->GSLrandom;
    /*
    Vectors to store wavelet parameters.
    Order:
    [0] t0
    [1] f0
    [2] Q
    [3] Amp
    [4] phi0
    */
    REAL8Vector *params_x = XLALCreateREAL8Vector(5);
    REAL8Vector *params_y = XLALCreateREAL8Vector(5);

    REAL8Vector *sigmas_x = XLALCreateREAL8Vector(5);
    REAL8Vector *sigmas_y = XLALCreateREAL8Vector(5);

    /* Get glitch meta paramters (dimnsion, proposal) */
    INT4Vector *gsize = LALInferenceGetINT4VectorVariable(proposedParams, "glitch_size");

    glitchFD = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_FD");
    glitch_f = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
    glitch_Q = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
    glitch_A = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");
    glitch_t = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_t0");
    glitch_p = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_phi");

    Anorm = LALInferenceGetREAL8Variable(thread->priorArgs, "glitch_norm");

    /* Choose which IFO */
    ifo = (INT4)floor(gsl_rng_uniform(rng) * (REAL8)(gsize->length));

    /* Bail out of proposal if no wavelets */
    if (gsize->data[ifo]==0) {
        XLALDestroyREAL8Vector(params_x);
        XLALDestroyREAL8Vector(params_y);
        XLALDestroyREAL8Vector(sigmas_x);
        XLALDestroyREAL8Vector(sigmas_y);

        return logPropRatio;
    }

    /* Choose which glitch */
    n = (INT4)floor(gsl_rng_uniform(rng) * (REAL8)(gsize->data[ifo]));

    /* Remove wavlet form linear combination */
    UpdateWaveletSum(thread, proposedParams, glitchFD, ifo, n, -1);

    /* Get parameters of n'th glitch int params vector */
    t0 = gsl_matrix_get(glitch_t, ifo, n); //Centroid time
    f0 = gsl_matrix_get(glitch_f, ifo, n); //Frequency
    Q = gsl_matrix_get(glitch_Q, ifo, n); //Quality
    Amp = gsl_matrix_get(glitch_A, ifo, n); //Amplitude
    phi0 = gsl_matrix_get(glitch_p, ifo, n); //Centroid phase


    /* Map to params Vector and compute Fisher */
    params_x->data[0] = t0;
    params_x->data[1] = f0;
    params_x->data[2] = Q;
    params_x->data[3] = Amp * (0.25*Anorm);//TODO: What is the 0.25*Anorm about?
    params_x->data[4] = phi0;

    MorletDiagonalFisherMatrix(params_x, sigmas_x);

    /* Jump from x -> y:  y = x + N[0,sigmas_x]*scale */
    scale = 0.4082482; // 1/sqrt(6)

    for(i=0; i<5; i++)
        params_y->data[i] = params_x->data[i] + gsl_ran_ugaussian(rng)*sigmas_x->data[i]*scale;

    /* Set parameters of n'th glitch int params vector */
    /* Map to params Vector and compute Fisher */
    t0   = params_y->data[0];
    f0   = params_y->data[1];
    Q    = params_y->data[2];
    Amp  = params_y->data[3]/(0.25*Anorm);
    phi0 = params_y->data[4];

    gsl_matrix_set(glitch_t, ifo, n, t0);
    gsl_matrix_set(glitch_f, ifo, n, f0);
    gsl_matrix_set(glitch_Q, ifo, n, Q);
    gsl_matrix_set(glitch_A, ifo, n, Amp);
    gsl_matrix_set(glitch_p, ifo, n, phi0);

    /* Add wavlet to linear combination */
    UpdateWaveletSum(thread, proposedParams, glitchFD, ifo, n, 1);

    /* Now compute proposal ratio using Fisher at y */
    MorletDiagonalFisherMatrix(params_y, sigmas_y);

    REAL8 sx  = 1.0; // sigma
    REAL8 sy  = 1.0;
    REAL8 dx  = 1.0; // (params_x - params_y)/sigma
    REAL8 dy  = 1.0;
    REAL8 exy = 0.0; // argument of exponential part of q
    REAL8 eyx = 0.0;
    REAL8 nxy = 1.0; // argument of normalization part of q
    REAL8 nyx = 1.0; // (computed as product to avoid too many log()s )
    for(i=0; i<5; i++) {
        sx = scale*sigmas_x->data[i];
        sy = scale*sigmas_y->data[i];

        dx = (params_x->data[i] - params_y->data[i])/sx;
        dy = (params_x->data[i] - params_y->data[i])/sy;

        nxy *= sy;
        nyx *= sx;

        exy += -dy*dy/2.0;
        eyx += -dx*dx/2.0;
    }

    qyx = eyx - log(nyx); //probabiltiy of proposing y given x
    qxy = exy - log(nxy); //probability of proposing x given y

    logPropRatio = qxy-qyx;

    XLALDestroyREAL8Vector(params_x);
    XLALDestroyREAL8Vector(params_y);

    XLALDestroyREAL8Vector(sigmas_x);
    XLALDestroyREAL8Vector(sigmas_y);

    return logPropRatio;
}

REAL8 LALInferenceGlitchMorletReverseJump(LALInferenceThreadState *thread,
                                          LALInferenceVariables *currentParams,
                                          LALInferenceVariables *proposedParams) {
    INT4 i,n;
    INT4 ifo;
    INT4 rj, nx, ny;
    REAL8 draw;
    REAL8 val, Anorm;
    REAL8 t=0, f=0, Q=0, A=0;
    REAL8 qx = 0.0; //log amp proposals
    REAL8 qy = 0.0;
    REAL8 qyx = 0.0; //log pixel proposals
    REAL8 qxy = 0.0;
    REAL8 pForward = 0.0; //combined p() & q() probabilities for ...
    REAL8 pReverse = 0.0; //...RJMCMC hastings ratio
    REAL8 logPropRatio = 0.0;
    INT4 adapting=1;
    gsl_matrix *params = NULL;

    gsl_rng *rng = thread->GSLrandom;
    LALInferenceVariables *propArgs = thread->proposalArgs;

    LALInferenceCopyVariables(currentParams, proposedParams);

    INT4Vector *gsize = LALInferenceGetINT4VectorVariable(proposedParams, "glitch_size");
    gsl_matrix *glitchFD = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_FD");

    INT4 nmin = (INT4)(LALInferenceGetREAL8Variable(thread->priorArgs,"glitch_dim_min"));
    INT4 nmax = (INT4)(LALInferenceGetREAL8Variable(thread->priorArgs,"glitch_dim_max"));

    if (LALInferenceCheckVariable(propArgs, "adapting"))
        adapting = LALInferenceGetINT4Variable(propArgs, "adapting");

    /* Choose which IFO */
    ifo = (INT4)floor(gsl_rng_uniform(rng) * (REAL8)(gsize->length) );
    nx = gsize->data[ifo];

    /* Choose birth or death move */
    draw = gsl_rng_uniform(rng);
    if (draw < 0.5)
        rj = 1;
    else
        rj = -1;

    /* find dimension of proposed model */
    ny = nx + rj;

    /* Check that new dimension is allowed */
    if(ny<nmin || ny>=nmax) {
        logPropRatio = -DBL_MAX;
        return logPropRatio;
    }

    switch(rj) {
        /* Birth */
        case 1:
            //Add new wavelet to glitch model
            t = draw_flat(thread, "morlet_t0_prior");
            f = draw_flat(thread, "morlet_f0_prior");

            //Centroid time
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_t0");
            gsl_matrix_set(params, ifo, nx, t);

            //Frequency
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
            gsl_matrix_set(params, ifo, nx, f);

            //Quality
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
            val = draw_flat(thread, "morlet_Q_prior");
            gsl_matrix_set(params, ifo, nx, val);
            Q = val;

            //Amplitude
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");
            val = glitchAmplitudeDraw(Q, f, rng);
            Anorm = LALInferenceGetREAL8Variable(thread->priorArgs, "glitch_norm");
            A = val/Anorm;

            gsl_matrix_set(params, ifo, nx, A);

            //Centroid phase
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_phi");
            val = draw_flat(thread, "morlet_phi_prior");
            gsl_matrix_set(params, ifo, nx, val);

            //Maximize phase, time, and amplitude using cross-correlation of data & wavelet
            if (adapting)
                MaximizeGlitchParameters(thread, proposedParams, ifo, nx);

            //Add wavlet to linear combination
            UpdateWaveletSum(thread, proposedParams, glitchFD, ifo, nx, 1);

            //Compute probability of drawing parameters
            qy = evaluate_morlet_proposal(thread, proposedParams, ifo, nx);// + log(gsl_matrix_get(power,ifo,k));

            //Compute reverse probability of dismissing k
            qxy = 0.0;//-log((double)runState->data->freqData->data->length);//-log( (REAL8)ny );

            if (adapting)
                qy += 10.0;

            break;

        /* Death */
        case -1:
            //Choose wavelet to remove from glitch model
            draw = gsl_rng_uniform(rng);
            n = (INT4)(floor(draw * (REAL8)nx));     //choose which hot pixel

            // Remove wavlet from linear combination
            UpdateWaveletSum(thread, proposedParams, glitchFD, ifo, n, -1);

            //Get t and f of removed wavelet
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
            f = gsl_matrix_get(params, ifo, n);
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_t0");
            t = gsl_matrix_get(params, ifo, n);
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
            Q = gsl_matrix_get(params, ifo, n);
            params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");
            A = gsl_matrix_get(params, ifo, n);

            //Shift morlet parameters to fill in array
            for(i=n; i<ny; i++) {
                params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_f0");
                gsl_matrix_set(params, ifo, i, gsl_matrix_get(params, ifo, i+1));
                params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Q");
                gsl_matrix_set(params, ifo, i, gsl_matrix_get(params, ifo, i+1));
                params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_Amp");
                gsl_matrix_set(params, ifo, i, gsl_matrix_get(params, ifo, i+1));
                params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_t0");
                gsl_matrix_set(params, ifo, i, gsl_matrix_get(params, ifo, i+1));
                params = LALInferenceGetgslMatrixVariable(proposedParams, "morlet_phi");
                gsl_matrix_set(params, ifo, i, gsl_matrix_get(params, ifo, i+1));
            }

            //Compute reverse probability of drawing parameters
            //find TF pixel

            qx = evaluate_morlet_proposal(thread, currentParams, ifo, n);// + log(gsl_matrix_get(power,ifo,k));

            //Compute forward probability of dismissing k
            qyx = 0.0;//-log((double)runState->data->freqData->data->length);//0.0;//-log( (REAL8)nx );

            if(adapting)
                qx += 10.0;

            break;

        default:
            break;
    }

    /* Update proposal structure for return to MCMC */

    //Update model meta-date
    gsize->data[ifo] = ny;

    //Re-package prior and proposal ratios into runState
    pForward = qxy + qx;
    pReverse = qyx + qy;

    logPropRatio = pForward-pReverse;

    return logPropRatio;
}

REAL8 LALInferencePolarizationPhaseJump(UNUSED LALInferenceThreadState *thread,
                                        LALInferenceVariables *currentParams,
                                        LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio = 0.0;

    LALInferenceCopyVariables(currentParams, proposedParams);

    REAL8 psi = LALInferenceGetREAL8Variable(proposedParams, "polarisation");
    REAL8 phi = LALInferenceGetREAL8Variable(proposedParams, "phase");

    phi += M_PI;
    psi += M_PI/2;

    phi = fmod(phi, 2.0*M_PI);
    psi = fmod(psi, M_PI);

    LALInferenceSetVariable(proposedParams, "polarisation", &psi);
    LALInferenceSetVariable(proposedParams, "phase", &phi);

    return logPropRatio;
}

REAL8 LALInferenceCorrPolarizationPhaseJump(LALInferenceThreadState *thread,
                                            LALInferenceVariables *currentParams,
                                            LALInferenceVariables *proposedParams) {
    REAL8 alpha,beta;
    REAL8 draw;
    REAL8 psi, phi;
    REAL8 logPropRatio = 0.0;

    LALInferenceCopyVariables(currentParams, proposedParams);

    gsl_rng *rng = thread->GSLrandom;

    psi = LALInferenceGetREAL8Variable(proposedParams, "polarisation");
    phi = LALInferenceGetREAL8Variable(proposedParams, "phase");

    alpha = psi + phi;
    beta  = psi - phi;

    //alpha =>   0:3pi
    //beta  => -2pi:pi

    //big jump in either alpha (beta) or beta (alpha)
    draw = gsl_rng_uniform(rng);
    if (draw < 0.5)
        alpha = gsl_rng_uniform(rng)*3.0*LAL_PI;
    else
        beta = -LAL_TWOPI + gsl_rng_uniform(rng)*3.0*LAL_PI;

    //transform back to psi,phi space
    psi = (alpha + beta)*0.5;
    phi = (alpha - beta)*0.5;

    //map back in range
    LALInferenceCyclicReflectiveBound(proposedParams, thread->priorArgs);

    LALInferenceSetVariable(proposedParams, "polarisation", &psi);
    LALInferenceSetVariable(proposedParams, "phase", &phi);

    return logPropRatio;
}

REAL8 LALInferenceFrequencyBinJump(LALInferenceThreadState *thread,
                                   LALInferenceVariables *currentParams,
                                   LALInferenceVariables *proposedParams) {
    REAL8 f0, df;
    REAL8 plusminus;
    REAL8 logPropRatio = 0.0;

    LALInferenceCopyVariables(currentParams, proposedParams);

    f0 = LALInferenceGetREAL8Variable(proposedParams, "f0");
    df = LALInferenceGetREAL8Variable(proposedParams, "df");

    plusminus = gsl_rng_uniform(thread->GSLrandom);
    if ( plusminus < 0.5 )
        f0 -= df;
    else
        f0 += df;

    LALInferenceSetVariable(proposedParams, "f0", &f0);

    return logPropRatio;
}

//This proposal needs to be called with exactly 3 independent detector locations.
static void reflected_extrinsic_parameters(LALInferenceThreadState *thread, const REAL8 ra, const REAL8 dec,
                                           const REAL8 baryTime, const REAL8 dist, const REAL8 iota, const REAL8 psi,
                                           REAL8 *newRA, REAL8 *newDec, REAL8 *newTime,
                                           REAL8 *newDist, REAL8 *newIota, REAL8 *newPsi) {
    REAL8 R2[4];
    REAL8 dist2;
    REAL8 gmst, newGmst;
    REAL8 cosIota, cosIota2;
    REAL8 Fplus, Fcross, psi_temp;
    REAL8 x[4], y[4], x2[4], y2[4];
    REAL8 newFplus[4], newFplus2[4], newFcross[4], newFcross2[4];
    REAL8 a, a2, b, c12;
    REAL8 cosnewIota, cosnewIota2;
    LIGOTimeGPS GPSlal;
    INT4 nUniqueDet, det;
    LALDetector *detectors;

    detectors = (LALDetector *)LALInferenceGetVariable(thread->proposalArgs, "detectors");
    nUniqueDet = LALInferenceGetINT4Variable(thread->proposalArgs, "nUniqueDet");

    XLALGPSSetREAL8(&GPSlal, baryTime);
    gmst = XLALGreenwichMeanSiderealTime(&GPSlal);

    reflected_position_and_time(thread, ra, dec, baryTime, newRA, newDec, newTime);

    XLALGPSSetREAL8(&GPSlal, *newTime);
    newGmst = XLALGreenwichMeanSiderealTime(&GPSlal);

    dist2 = dist*dist;

    cosIota = cos(iota);
    cosIota2 = cosIota*cosIota;

    /* Loop over interferometers */
    INT4 i=1, j=0;
    for (det=0; det < nUniqueDet; det++) {
        psi_temp = 0.0;

        XLALComputeDetAMResponse(&Fplus, &Fcross, (const REAL4(*)[3])detectors[det].response, *newRA, *newDec, psi_temp, newGmst);
        j=i-1;
        while (j>0){
            if (Fplus==x[j]){
                det++;
                XLALComputeDetAMResponse(&Fplus, &Fcross, (const REAL4(*)[3])detectors[det].response, *newRA, *newDec, psi_temp, newGmst);
            }
            j--;
        }
        x[i]=Fplus;
        x2[i]=Fplus*Fplus;
        y[i]=Fcross;
        y2[i]=Fcross*Fcross;

        XLALComputeDetAMResponse(&Fplus, &Fcross, (const REAL4(*)[3])detectors[det].response, ra, dec, psi, gmst);
        R2[i] = (((1.0+cosIota2)*(1.0+cosIota2))/(4.0*dist2))*Fplus*Fplus
                + ((cosIota2)/(dist2))*Fcross*Fcross;

        i++;
    }

    a = (R2[3]*x2[2]*y2[1] - R2[2]*x2[3]*y2[1] - R2[3]*x2[1]*y2[2] + R2[1]*x2[3]*y2[2] + R2[2]*x2[1]*y2[3] -
        R2[1]*x2[2]*y2[3]);
    a2 = a*a;
    b = (-(R2[3]*x[1]*x2[2]*y[1]) + R2[2]*x[1]*x2[3]*y[1] + R2[3]*x2[1]*x[2]*y[2] - R2[1]*x[2]*x2[3]*y[2] +
        R2[3]*x[2]*y2[1]*y[2] - R2[3]*x[1]*y[1]*y2[2] - R2[2]*x2[1]*x[3]*y[3] + R2[1]*x2[2]*x[3]*y[3] - R2[2]*x[3]*y2[1]*y[3] + R2[1]*x[3]*y2[2]*y[3] +
        R2[2]*x[1]*y[1]*y2[3] - R2[1]*x[2]*y[2]*y2[3]);

    (*newPsi) = (2.*atan((b - a*sqrt((a2 + b*b)/(a2)))/a))/4.;

    while ((*newPsi)<0){
        (*newPsi)=(*newPsi)+LAL_PI/4.0;
    }

    while ((*newPsi)>LAL_PI/4.0){
        (*newPsi)=(*newPsi)-LAL_PI/4.0;
    }

    for (i = 1; i < 4; i++){
        newFplus[i] = x[i]*cos(2.0*(*newPsi)) + y[i]*sin(2.0*(*newPsi));
        newFplus2[i] = newFplus[i] * newFplus[i];

        newFcross[i] = y[i]*cos(2.0*(*newPsi)) - x[i]*sin(2.0*(*newPsi));
        newFcross2[i] = newFcross[i] * newFcross[i];
    }

    c12 = -2.0*((R2[1]*(newFcross2[2])-R2[2]*(newFcross2[1]))
          /(R2[1]*(newFplus2[2])-R2[2]*(newFplus2[1])))-1.0;

    if (c12<1.0){
        c12 = (3.0-c12)/(1.0+c12);
        (*newPsi) = (*newPsi)+LAL_PI/4.0;

        for (i = 1; i < 4; i++){
            newFplus[i] = x[i]*cos(2.0*(*newPsi)) + y[i]*sin(2.0*(*newPsi));
            newFplus2[i] = newFplus[i] * newFplus[i];

            newFcross[i] = y[i]*cos(2.0*(*newPsi)) - x[i]*sin(2.0*(*newPsi));
            newFcross2[i] = newFcross[i] * newFcross[i];
        }
    }

    if (c12<1){
        *newIota = iota;
        *newDist = dist;
        return;
    }

    cosnewIota2 = c12-sqrt(c12*c12-1.0);
    cosnewIota = sqrt(cosnewIota2);
    *newIota = acos(cosnewIota);

    *newDist = sqrt((
                    ((((1.0+cosnewIota2)*(1.0+cosnewIota2))/(4.0))*newFplus2[1]
                    + (cosnewIota2)*newFcross2[1])
                    )/ R2[1]);

    if (Fplus*newFplus[3]<0){
        (*newPsi)=(*newPsi)+LAL_PI/2.;
        newFcross[3]=-newFcross[3];
    }

    if (Fcross*cosIota*cosnewIota*newFcross[3]<0){
        (*newIota)=LAL_PI-(*newIota);
    }
}


REAL8 LALInferenceExtrinsicParamProposal(LALInferenceThreadState *thread,
                                         LALInferenceVariables *currentParams,
                                         LALInferenceVariables *proposedParams) {
    INT4 nUniqueDet;
    INT4 timeflag=0;
    REAL8 baryTime;
    REAL8 ra, dec;
    REAL8 psi, dist;
    REAL8 newRA, newDec, newTime, newDist, newIota, newPsi;
    REAL8 nRA, nDec, nTime, nDist, nIota, nPsi;
    REAL8 refRA, refDec, refTime, refDist, refIota, refPsi;
    REAL8 nRefRA, nRefDec, nRefTime, nRefDist, nRefIota, nRefPsi;
    REAL8 pForward, pReverse;
    REAL8 cst;
    REAL8 iota=0.0;
    REAL8 logPropRatio = 0.0;
    /* Find the number of distinct-position detectors. */
    /* Exit with same parameters (with a warning the first time) if
       there are not EXACTLY three unique detector locations. */
    static INT4 warningDelivered = 0;
    LIGOTimeGPS *epoch;

    LALInferenceCopyVariables(currentParams, proposedParams);

    LALInferenceVariables *args = thread->proposalArgs;
    gsl_rng *rng = thread->GSLrandom;
    epoch = (LIGOTimeGPS *)LALInferenceGetVariable(args, "epoch");

    nUniqueDet = LALInferenceGetINT4Variable(args, "nUniqueDet");
    if (nUniqueDet != 3) {
        if (!warningDelivered) {
            fprintf(stderr, "WARNING: trying to reflect through the decector plane with %d\n", nUniqueDet);
            fprintf(stderr, "WARNING: geometrically independent locations,\n");
            fprintf(stderr, "WARNING: but this proposal should only be used with exactly 3 independent detectors.\n");
            fprintf(stderr, "WARNING: %s, line %d\n", __FILE__, __LINE__);
            warningDelivered = 1;
        }

        return logPropRatio;
    }

    ra = LALInferenceGetREAL8Variable(proposedParams, "rightascension");
    dec = LALInferenceGetREAL8Variable(proposedParams, "declination");

    if (LALInferenceCheckVariable(proposedParams,"time")){
        baryTime = LALInferenceGetREAL8Variable(proposedParams, "time");
        timeflag = 1;
    } else {
        baryTime = XLALGPSGetREAL8(epoch);
    }

    if (LALInferenceCheckVariable(proposedParams,"costheta_jn"))
        iota = acos(LALInferenceGetREAL8Variable(proposedParams, "costheta_jn"));
    else
        fprintf(stderr, "LALInferenceExtrinsicParamProposal: No  theta_jn parameter!\n");

    psi = LALInferenceGetREAL8Variable(proposedParams, "polarisation");

    dist = exp(LALInferenceGetREAL8Variable(proposedParams, "logdistance"));

    reflected_extrinsic_parameters(thread, ra, dec, baryTime, dist, iota, psi, &newRA, &newDec, &newTime, &newDist, &newIota, &newPsi);

    /* Unit normal deviates, used to "fuzz" the state. */
    const REAL8 epsDist = 1e-8;
    const REAL8 epsTime = 1e-8;
    const REAL8 epsAngle = 1e-8;

    nRA = gsl_ran_ugaussian(rng);
    nDec = gsl_ran_ugaussian(rng);
    nTime = gsl_ran_ugaussian(rng);
    nDist = gsl_ran_ugaussian(rng);
    nIota = gsl_ran_ugaussian(rng);
    nPsi = gsl_ran_ugaussian(rng);

    newRA += epsAngle*nRA;
    newDec += epsAngle*nDec;
    newTime += epsTime*nTime;
    newDist += epsDist*nDist;
    newIota += epsAngle*nIota;
    newPsi += epsAngle*nPsi;

    /* And the doubly-reflected position (near the original, but not
    exactly due to the fuzzing). */
    reflected_extrinsic_parameters(thread, newRA, newDec, newTime, newDist, newIota, newPsi, &refRA, &refDec, &refTime, &refDist, &refIota, &refPsi);

    /* The Gaussian increments required to shift us back to the original
    position from the doubly-reflected position. */
    nRefRA = (ra - refRA)/epsAngle;
    nRefDec = (dec - refDec)/epsAngle;
    nRefTime = (baryTime - refTime)/epsTime;
    nRefDist = (dist - refDist)/epsDist;
    nRefIota = (iota - refIota)/epsAngle;
    nRefPsi = (psi - refPsi)/epsAngle;

    cst = log(1./(sqrt(2.*LAL_PI)));
    pReverse = 6*cst-0.5*(nRefRA*nRefRA+nRefDec*nRefDec+nRefTime*nRefTime+nRefDist*nRefDist+nRefIota*nRefIota+nRefPsi*nRefPsi);
    pForward = 6*cst-0.5*(nRA*nRA+nDec*nDec+nTime*nTime+nDist*nDist+nIota*nIota+nPsi*nPsi);

    LALInferenceSetVariable(proposedParams, "rightascension", &newRA);
    LALInferenceSetVariable(proposedParams, "declination", &newDec);
    if (timeflag)
        LALInferenceSetVariable(proposedParams, "time", &newTime);

    REAL8 logNewDist = log(newDist);
    LALInferenceSetVariable(proposedParams, "logdistance", &logNewDist);

    REAL8 newcosIota = cos(newIota);
    LALInferenceSetVariable(proposedParams, "costheta_jn", &newcosIota);
    LALInferenceSetVariable(proposedParams, "polarisation", &newPsi);

    logPropRatio = pReverse - pForward;

    return logPropRatio;
}


void LALInferenceSetupGlitchProposal(LALInferenceIFOData *data, LALInferenceVariables *propArgs) {
    INT4 i, nDet;
    REAL8Vector *flows, *fhighs;
    REAL8FrequencySeries **asds, **psds;
    REAL8TimeSeries **td_data;
    COMPLEX16FrequencySeries **fd_data;
    REAL8FFTPlan **plans;

    nDet = LALInferenceGetINT4Variable(propArgs, "nDet");

    flows = XLALCreateREAL8Vector(nDet);
    fhighs = XLALCreateREAL8Vector(nDet);
    asds = XLALCalloc(nDet, sizeof(REAL8FrequencySeries *));
    psds = XLALCalloc(nDet, sizeof(REAL8FrequencySeries *));
    td_data = XLALCalloc(nDet, sizeof(REAL8TimeSeries *));
    fd_data = XLALCalloc(nDet, sizeof(COMPLEX16FrequencySeries *));
    plans = XLALCalloc(nDet, sizeof(REAL8FFTPlan *));

    for (i=0; i<nDet; i++) {
        flows->data[i] = data->fLow;
        fhighs->data[i] = data->fHigh;

        asds[i] = data->noiseASD;
        psds[i] = data->oneSidedNoisePowerSpectrum;

        td_data[i] = data->timeData;
        fd_data[i] = data->freqData;

        plans[i] = data->freqToTimeFFTPlan;
        data = data->next;
    }

    LALInferenceAddREAL8VectorVariable(propArgs, "flows", flows, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddREAL8VectorVariable(propArgs, "fhighs", fhighs, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddVariable(propArgs, "asds", asds, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddVariable(propArgs, "psds", psds, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddVariable(propArgs, "td_data", td_data, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddVariable(propArgs, "fd_data", fd_data, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);
    LALInferenceAddVariable(propArgs, "f2t_plans", plans, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_FIXED);

}

/* Initialize differential evolution proposal */
void LALInferenceSetupDifferentialEvolutionProposal(LALInferenceThreadState *thread) {
    thread->differentialPoints = XLALCalloc(1, sizeof(LALInferenceVariables *));
    thread->differentialPointsLength = 0;
    thread->differentialPointsSize = 1;
}


/** Setup adaptive proposals. Should be called when state->currentParams is already filled with an initial sample */
void LALInferenceSetupAdaptiveProposals(LALInferenceVariables *propArgs, LALInferenceVariables *params) {
    INT4 no_adapt, adapting;
    INT4 adaptTau, adaptLength, adaptResetBuffer, adaptStart;
    REAL8 sigma, s_gamma;
    REAL8 logLAtAdaptStart = -DBL_MAX;

    char *nameBuffer;

    LALInferenceVariableItem *this;

    for(this=params->head; this; this=this->next) {
        char *name = this->name;

        if (!strcmp(name, "eta") || !strcmp(name, "q") || !strcmp(name, "time") || !strcmp(name, "a_spin2") || !strcmp(name, "a_spin1")){
            sigma = 0.001;
        } else if (!strcmp(name, "polarisation") || !strcmp(name, "phase") || !strcmp(name, "costheta_jn")){
            sigma = 0.1;
        } else {
            sigma = 0.01;
        }

        /* Set up variables to store current sigma, proposed and accepted */
        char varname[MAX_STRLEN] = "";
        sprintf(varname, "%s_%s", name, ADAPTSUFFIX);
        LALInferenceAddREAL8Variable(propArgs, varname, sigma, LALINFERENCE_PARAM_LINEAR);

        sigma = 0.0;
        sprintf(varname, "%s_%s", name, ACCEPTSUFFIX);
        LALInferenceAddREAL8Variable(propArgs, varname, sigma, LALINFERENCE_PARAM_LINEAR);

        sprintf(varname, "%s_%s", name, PROPOSEDSUFFIX);
        LALInferenceAddREAL8Variable(propArgs, varname, sigma, LALINFERENCE_PARAM_LINEAR);
    }

    no_adapt = LALInferenceGetINT4Variable(propArgs, "no_adapt");
    adapting = !no_adapt;      // Indicates if current iteration is being adapted
    LALInferenceAddINT4Variable(propArgs, "adapting", adapting, LALINFERENCE_PARAM_LINEAR);

    nameBuffer = XLALCalloc(MAX_STRLEN, sizeof(char));
    sprintf(nameBuffer, "none");
    LALInferenceAddstringVariable(propArgs, "proposedVariableName", nameBuffer, LALINFERENCE_PARAM_OUTPUT);

    adaptTau = LALInferenceGetINT4Variable(propArgs, "adaptTau");  // Sets decay of adaption function
    adaptLength = pow(10, adaptTau);  // Number of iterations to adapt before turning off
    adaptResetBuffer = 100; // Number of iterations before adapting after a restart
    s_gamma = 1.0; // Sets the size of changes to jump size during adaptation
    adaptStart = 0; // Keeps track of last iteration adaptation was restarted

    LALInferenceAddINT4Variable(propArgs, "adaptLength", adaptLength, LALINFERENCE_PARAM_LINEAR);
    LALInferenceAddINT4Variable(propArgs, "adaptResetBuffer", adaptResetBuffer, LALINFERENCE_PARAM_LINEAR);
    LALInferenceAddREAL8Variable(propArgs, "s_gamma", s_gamma, LALINFERENCE_PARAM_LINEAR);
    LALInferenceAddINT4Variable(propArgs, "adaptStart", adaptStart, LALINFERENCE_PARAM_LINEAR);
    LALInferenceAddREAL8Variable(propArgs, "logLAtAdaptStart", logLAtAdaptStart, LALINFERENCE_PARAM_LINEAR);

    return;
}


/** Update proposal statistics if tracking */
void LALInferenceTrackProposalAcceptance(LALInferenceThreadState *thread) {
    INT4 i = 0;

    LALInferenceProposal *prop = thread->cycle->proposals[i];

    /* Find the proposal that was last called (by name) */
    while (strcmp(prop->name, thread->cycle->last_proposal)) {
        i++;
        prop = thread->cycle->proposals[i];
    }

    /* Update proposal statistics */
    prop->proposed++;
    if (thread->accepted == 1){
        prop->accepted++;
    }

    return;
}

/* Zero out proposal statistics counters */
void LALInferenceZeroProposalStats(LALInferenceProposalCycle *cycle) {
    INT4 i=0;

    for (i=0; i<cycle->nProposals; i++) {
        LALInferenceProposal *prop = cycle->proposals[i];

        prop->proposed = 0;
        prop->accepted = 0;
    }

    return;
}

/** Update the adaptive proposal. Whether or not a jump was accepted is passed with accepted */
void LALInferenceUpdateAdaptiveJumps(LALInferenceThreadState *thread, REAL8 targetAcceptance){
    INT4 *adaptableStep = NULL;
    INT4 adapting = 0;
    REAL8 priorMin, priorMax, dprior, s_gamma;
    REAL8 *accept, *propose, *sigma;
    char *name;

    LALInferenceVariables *args = thread->proposalArgs;

    if (LALInferenceCheckVariable(args, "adaptableStep" ) &&
        LALInferenceCheckVariable(args, "adapting" )){
        adaptableStep = (INT4 *)LALInferenceGetVariable(args, "adaptableStep");
        adapting = LALInferenceGetINT4Variable(args, "adapting");
    }
    /* Don't do anything if these are not found */
    else return;

    if (adaptableStep && adapting) {
        name = LALInferenceGetstringVariable(thread->proposalArgs, "proposedVariableName");
        char tmpname[MAX_STRLEN] = "";

        sprintf(tmpname, "%s_%s", name, PROPOSEDSUFFIX);

        propose = (REAL8 *)LALInferenceGetVariable(args, tmpname);
        *propose += 1;

        sprintf(tmpname, "%s_%s", name, ACCEPTSUFFIX);

        accept = (REAL8 *)LALInferenceGetVariable(args, tmpname);

        if (thread->accepted == 1)
            *accept += 1;
    }

    /* Adapt if desired. */
    if (LALInferenceCheckVariable(args, "proposedVariableName") &&
        LALInferenceCheckVariable(args, "s_gamma") &&
        LALInferenceCheckVariable(args, "adapting") &&
        LALInferenceCheckVariable(args, "adaptableStep")) {

        if (*adaptableStep) {
            name = *(char **)LALInferenceGetVariable(args, "proposedVariableName");
            char tmpname[MAX_STRLEN]="";

            s_gamma = LALInferenceGetREAL8Variable(args, "s_gamma");
            sigma = (REAL8 *)LALInferenceGetVariable(args,tmpname);

            sprintf(tmpname, "%s_%s", name, ADAPTSUFFIX);

            LALInferenceGetMinMaxPrior(thread->priorArgs, name, &priorMin, &priorMax);
            dprior = priorMax - priorMin;

            if (thread->accepted == 1){
                *sigma = *sigma + s_gamma * (dprior/100.0) * (1.0-targetAcceptance);
            } else {
                *sigma = *sigma - s_gamma * (dprior/100.0) * (targetAcceptance);
            }

            *sigma = (*sigma > dprior ? dprior : *sigma);
            *sigma = (*sigma < DBL_MIN ? DBL_MIN : *sigma);

            /* Make sure we don't do this again until we take another adaptable step.*/
        }
    }

    *adaptableStep = 0;
}


// /**
//  * Setup all clustered-KDE proposals with samples read from file.
//  *
//  * Constructed clustered-KDE proposals from all sample lists provided in
//  * files given on the command line.
//  * @param runState The LALInferenceRunState to get command line options from and to the proposal cycle of.
//  */
// void LALInferenceSetupClusteredKDEProposalsFromFile(LALInferenceThreadState *thread, FILE *inp) {
//     LALInferenceVariableItem *item;
//     INT4 i=0, j=0, k=0;
//     INT4 nBurnins=0, nWeights=0, nPostEsts=0;
//     INT4 inChain;
//     INT4 burnin;
//     INT4 cyclic_reflective = LALInferenceGetINT4Variable(thread->proposalArgs, "cyclic_reflective_kde");
//     REAL8 weight;
//     ProcessParamsTable *command;
//
//     /* Loop once to get number of sample files and sanity check.
//      *   If PTMCMC files, only load this chain's file.  Also check
//      *   if cyclic/reflective bounds have been requested */
//     nPostEsts=0;
//     for(command=runState->commandLine; command; command=command->next) {
//         if(!strcmp(command->param, "--ptmcmc-samples")) {
//             inChain = atoi(strrchr(command->value, '.')+1);
//             if (chain == inChain) nPostEsts++;
//         } else if (!strcmp(command->param, "--ascii-samples")) {
//             nPostEsts++;
//     }
//
//     INT4 *burnins = XLALCalloc(nPostEsts, sizeof(INT4));
//     INT4 *weights = XLALCalloc(nPostEsts, sizeof(INT4));
//
//     /* Get burnins and weights */
//     for(command=runState->commandLine; command; command=command->next) {
//       if(!strcmp(command->param, "--input-burnin")) {
//         if (nBurnins < nPostEsts) {
//           burnins[nBurnins] = atoi(command->value);
//           nBurnins++;
//         } else {
//           nBurnins++;
//           break;
//         }
//       } else if (!strcmp(command->param, "--input-weight")) {
//         if (nWeights < nPostEsts) {
//           weights[nWeights] = atoi(command->value);
//           nWeights++;
//         } else {
//           nWeights++;
//           break;
//         }
//       }
//     }
//
//     if (nBurnins > 0 && nBurnins != nPostEsts) { fprintf(stderr, "Inconsistent number of posterior sample files and burnins given!\n"); exit(1); }
//     if (nWeights > 0 && nWeights != nPostEsts) { fprintf(stderr, "Inconsistent number of posterior sample files and weights given!\n"); exit(1); }
//
//     /* Assign equal weighting if none specified. */
//     if (nWeights == 0) {
//         weight = 1.;
//         for (i=0; i<nPostEsts; i++)
//             weights[i] = weight;
//     }
//
//     i=0;
//     for(command=runState->commandLine; command; command=command->next) {
//         if(!strcmp(command->param, "--ptmcmc-samples") || !strcmp(command->param, "--ascii-samples")) {
//             INT4 ptmcmc = 0;
//             if (!strcmp(command->param, "--ptmcmc-samples")) {
//                 inChain = atoi(strrchr(command->value, '.')+1);
//                 if (inChain != chain)
//                     continue;
//
//                 ptmcmc = 1;
//             }
//
//             LALInferenceClusteredKDE *kde = XLALCalloc(1, sizeof(LALInferenceClusteredKDE));
//
//             weight = weights[i];
//             if (nBurnins > 0)
//                 burnin = burnins[i];
//             else
//                 burnin = 0;
//
//             char *infilename = command->value;
//             FILE *input = fopen(infilename, "r");
//
//             char *propName = XLALCalloc(512, sizeof(char));
//             sprintf(propName, "%s_%s", clusteredKDEProposalName, infilename);
//
//             INT4 nInSamps;
//             INT4 nCols;
//             REAL8 *sampleArray;
//
//             if (ptmcmc)
//                 LALInferenceDiscardPTMCMCHeader(input);
//
//             char params[128][VARNAME_MAX];
//             LALInferenceReadAsciiHeader(input, params, &nCols);
//
//             LALInferenceVariables *backwardClusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));
//
//             /* Only cluster parameters that are being sampled */
//             INT4 nValidCols=0;
//             INT4 *validCols = XLALCalloc(nCols, sizeof(INT4));
//             for (j=0; j<nCols; j++)
//                 validCols[j] = 0;
//
//             INT4 logl_idx = 0;
//             for (j=0; j<nCols; j++) {
//                 if (!strcmp("logl", params[j])) {
//                     logl_idx = j;
//                     continue;
//                 }
//
//                 char* internal_param_name = XLALCalloc(512, sizeof(char));
//                 LALInferenceTranslateExternalToInternalParamName(internal_param_name, params[j]);
//
//                 for (item = runState->currentParams->head; item; item = item->next) {
//                     if (!strcmp(item->name, internal_param_name) &&
//                         LALInferenceCheckVariableNonFixed(runState->currentParams, item->name)) {
//                         nValidCols++;
//                         validCols[j] = 1;
//                         LALInferenceAddVariable(backwardClusterParams, item->name, item->value, item->type, item->vary);
//                         break;
//                     }
//                 }
//             }
//
//             /* LALInferenceAddVariable() builds the array backwards, so reverse it. */
//             LALInferenceVariables *clusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));
//
//             for (item = backwardClusterParams->head; item; item = item->next)
//                 LALInferenceAddVariable(clusterParams, item->name, item->value, item->type, item->vary);
//
//             /* Burn in samples and parse the remainder */
//             if (ptmcmc)
//                 LALInferenceBurninPTMCMC(input, logl_idx, nValidCols);
//             else
//                 LALInferenceBurninStream(input, burnin);
//
//             sampleArray = LALInferenceParseDelimitedAscii(input, nCols, validCols, &nInSamps);
//
//             /* Downsample PTMCMC file to have independent samples */
//             if (ptmcmc) {
//                 INT4 acl = (INT4)LALInferenceComputeMaxAutoCorrLen(sampleArray, nInSamps, nValidCols);
//                 if (acl < 1) acl = 1;
//                 INT4 downsampled_size = ceil((REAL8)nInSamps/acl);
//                 REAL8 *downsampled_array = (REAL8 *)XLALCalloc(downsampled_size * nValidCols, sizeof(REAL8));
//                 printf("Chain %i downsampling to achieve %i samples.\n", chain, downsampled_size);
//                 for (k=0; k < downsampled_size; k++) {
//                     for (j=0; j < nValidCols; j++)
//                         downsampled_array[k*nValidCols + j] = sampleArray[k*nValidCols*acl + j];
//                 }
//                 XLALFree(sampleArray);
//                 sampleArray = downsampled_array;
//                 nInSamps = downsampled_size;
//             }
//
//             /* Build the KDE estimate and add to the KDE proposal set */
//             INT4 ntrials = 50;  // Number of trials at fixed-k to find optimal BIC
//             LALInferenceInitClusteredKDEProposal(runState, kde, sampleArray, nInSamps, clusterParams, propName, weight, LALInferenceOptimizedKmeans, cyclic_reflective, ntrials);
//
//             /* If kmeans construction failed, halt the run */
//             if (!kde->kmeans) {
//                 fprintf(stderr, "\nERROR: Couldn't build kmeans clustering from the file specified.\n");
//                 XLALFree(kde);
//                 XLALFree(burnins);
//                 XLALFree(weights);
//                 exit(-1);
//             }
//
//             LALInferenceAddClusteredKDEProposalToSet(runState, kde);
//
//             LALInferenceClearVariables(backwardClusterParams);
//             XLALFree(backwardClusterParams);
//             XLALFree(propName);
//             XLALFree(sampleArray);
//
//             i++;
//         }
//     }
//
//     XLALFree(burnins);
//     XLALFree(weights);
//     printf("done\n");
// }


/**
 * Setup all clustered-KDE proposals with samples read from file.
 *
 * Constructed clustered-KDE proposals from all sample lists provided in
 * files given on the command line.
 * @param thread The LALInferenceThreadState to get command line options from and to the proposal cycle of.
 */
void LALInferenceSetupClusteredKDEProposalsFromASCII(LALInferenceThreadState *thread, FILE *input, INT4 burnin, REAL8 weight, INT4 ptmcmc) {
    LALInferenceVariableItem *item;
    INT4 j=0, k=0;

    INT4 cyclic_reflective = LALInferenceGetINT4Variable(thread->proposalArgs, "cyclic_reflective_kde");

    LALInferenceClusteredKDE *kde = XLALCalloc(1, sizeof(LALInferenceClusteredKDE));

    INT4 nInSamps;
    INT4 nCols;
    REAL8 *sampleArray;

    if (ptmcmc)
        LALInferenceDiscardPTMCMCHeader(input);

    char params[128][VARNAME_MAX];
    LALInferenceReadAsciiHeader(input, params, &nCols);

    LALInferenceVariables *backwardClusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));

    /* Only cluster parameters that are being sampled */
    INT4 nValidCols=0;
    INT4 *validCols = XLALCalloc(nCols, sizeof(INT4));
    for (j=0; j<nCols; j++)
        validCols[j] = 0;

    INT4 logl_idx = 0;
    for (j=0; j<nCols; j++) {
        if (!strcmp("logl", params[j])) {
            logl_idx = j;
            continue;
        }

        char* internal_param_name = XLALCalloc(512, sizeof(char));
        LALInferenceTranslateExternalToInternalParamName(internal_param_name, params[j]);

        for (item = thread->currentParams->head; item; item = item->next) {
            if (!strcmp(item->name, internal_param_name) &&
                LALInferenceCheckVariableNonFixed(thread->currentParams, item->name)) {
                nValidCols++;
                validCols[j] = 1;
                LALInferenceAddVariable(backwardClusterParams, item->name, item->value, item->type, item->vary);
                break;
            }
        }
    }

    /* LALInferenceAddVariable() builds the array backwards, so reverse it. */
    LALInferenceVariables *clusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));

    for (item = backwardClusterParams->head; item; item = item->next)
        LALInferenceAddVariable(clusterParams, item->name, item->value, item->type, item->vary);

    /* Burn in samples and parse the remainder */
    if (ptmcmc)
        LALInferenceBurninPTMCMC(input, logl_idx, nValidCols);
    else
        LALInferenceBurninStream(input, burnin);

    sampleArray = LALInferenceParseDelimitedAscii(input, nCols, validCols, &nInSamps);

    /* Downsample PTMCMC file to have independent samples */
    if (ptmcmc) {
        INT4 acl = (INT4)LALInferenceComputeMaxAutoCorrLen(sampleArray, nInSamps, nValidCols);
        if (acl < 1) acl = 1;
        INT4 downsampled_size = ceil((REAL8)nInSamps/acl);
        REAL8 *downsampled_array = (REAL8 *)XLALCalloc(downsampled_size * nValidCols, sizeof(REAL8));
        printf("Downsampling to achieve %i samples.\n", downsampled_size);
        for (k=0; k < downsampled_size; k++) {
            for (j=0; j < nValidCols; j++)
                downsampled_array[k*nValidCols + j] = sampleArray[k*nValidCols*acl + j];
        }
        XLALFree(sampleArray);
        sampleArray = downsampled_array;
        nInSamps = downsampled_size;
    }

    /* Build the KDE estimate and add to the KDE proposal set */
    INT4 ntrials = 50;  // Number of trials at fixed-k to find optimal BIC
    LALInferenceInitClusteredKDEProposal(thread, kde, sampleArray, nInSamps, clusterParams, clusteredKDEProposalName, weight, LALInferenceOptimizedKmeans, cyclic_reflective, ntrials);

    /* If kmeans construction failed, halt the run */
    if (!kde->kmeans) {
        fprintf(stderr, "\nERROR: Couldn't build kmeans clustering from the file specified.\n");
        XLALFree(kde);
        exit(-1);
    }

    LALInferenceAddClusteredKDEProposalToSet(thread->proposalArgs, kde);

    LALInferenceClearVariables(backwardClusterParams);
    XLALFree(backwardClusterParams);
    XLALFree(sampleArray);
}


/**
 * Initialize a clustered-KDE proposal.
 *
 * Estimates the underlying distribution of a set of points with a clustered kernel density estimate
 * and constructs a jump proposal from the estimated distribution.
 * @param      thread   The current LALInferenceThreadState.
 * @param[out] kde      An empty proposal structure to populate with the clustered-KDE estimate.
 * @param[in]  array    The data to estimate the underlying distribution of.
 * @param[in]  nSamps   Number of samples contained in \a array.
 * @param[in]  params   The parameters contained in \a array.
 * @param[in]  name     The name of the proposal being constructed.
 * @param[in]  weight   The relative weight this proposal is to have against other KDE proposals.
 * @param[in]  cluster_method A pointer to the clustering function to be used.
 * @param[in]  cyclic_reflective Flag to check for cyclic/reflective bounds.
 * @param[in]  ntrials  Number of kmeans attempts at fixed k to find optimal BIC.
 */
void LALInferenceInitClusteredKDEProposal(LALInferenceThreadState *thread,
                                          LALInferenceClusteredKDE *kde,
                                          REAL8 *array,
                                          INT4 nSamps,
                                          LALInferenceVariables *params,
                                          const char *name,
                                          REAL8 weight,
                                          LALInferenceKmeans* (*cluster_method)(gsl_matrix*, INT4, gsl_rng*),
                                          INT4 cyclic_reflective,
                                          INT4 ntrials) {
    INT4 dim;
    INT4 ndraws = 1000;
    gsl_matrix_view mview;
    char outp_name[256];
    char outp_draws_name[256];

    strcpy(kde->name, name);
    dim = LALInferenceGetVariableDimensionNonFixed(params);

    /* If kmeans is already assigned, assume it was calculated elsewhere */
    if (!kde->kmeans) {
        mview = gsl_matrix_view_array(array, nSamps, dim);
        kde->kmeans = (*cluster_method)(&mview.matrix, ntrials, thread->GSLrandom);
    }

    /* Return if kmeans setup failed */
    if (!kde->kmeans)
        return;

    kde->dimension = kde->kmeans->dim;
    kde->params = params;

    kde->weight = weight;
    kde->next = NULL;

    /* Selectivey impose bounds on KDEs */
    LALInferenceKmeansImposeBounds(kde->kmeans, params, thread->priorArgs, cyclic_reflective);

    /* Print out clustered samples, assignments, and PDF values if requested */
    if (LALInferenceGetINT4Variable(thread->proposalArgs, "verbose")) {
        printf("Thread %i found %i clusters.\n", thread->id, kde->kmeans->k);

        sprintf(outp_name, "clustered_samples.%2.2d", thread->id);
        sprintf(outp_draws_name, "clustered_draws.%2.2d", thread->id);

        LALInferenceDumpClusteredKDE(kde, outp_name, array);
        LALInferenceDumpClusteredKDEDraws(kde, outp_draws_name, ndraws);
    }
}


/**
 * Dump draws from a KDE to file.
 *
 * Print out the samples used to estimate the distribution, along with their
 * cluster assignments, and the PDF evaluated at each sample.
 * @param[in] kde       The clustered KDE to dump the info of.
 * @param[in] outp_name The name of the output file.
 * @param[in] array     The array of samples used for the KDE (it only stores a whitened version).
 */
void LALInferenceDumpClusteredKDE(LALInferenceClusteredKDE *kde, char *outp_name, REAL8 *array) {
    FILE *outp;
    REAL8 PDF;
    INT4 i, j;

    outp = fopen(outp_name, "w");
    LALInferenceFprintParameterNonFixedHeaders(outp, kde->params);
    fprintf(outp, "cluster\tweight\tPDF\n");

    for (i=0; i<kde->kmeans->npts; i++) {
        PDF = LALInferenceKmeansPDF(kde->kmeans, array + i*kde->dimension);
        for (j=0; j<kde->dimension; j++)
            fprintf(outp, "%g\t", array[i*kde->dimension + j]);
        fprintf(outp, "%i\t%f\t%g\n", kde->kmeans->assignments[i], kde->kmeans->weights[kde->kmeans->assignments[i]], PDF);
    }
    fclose(outp);
}


/**
 * Dump clustered KDE information to file.
 *
 * Dump a requested number of draws from a clustered-KDE to file,
 * along with the value of the PDF at each point.
 * @param[in] kde        The clustered-KDE proposal to draw from.
 * @param[in] outp_name  The name of the file to write to.
 * @param[in] nSamps     The number of draws to write.
 */
void LALInferenceDumpClusteredKDEDraws(LALInferenceClusteredKDE *kde, char *outp_name, INT4 nSamps) {
    FILE *outp;
    INT4 i, j;
    REAL8 *draw, PDF;

    outp = fopen(outp_name, "w");
    LALInferenceFprintParameterNonFixedHeaders(outp, kde->params);
    fprintf(outp, "PDF\n");

    for (i=0; i<nSamps; i++) {
        draw = LALInferenceKmeansDraw(kde->kmeans);
        PDF = LALInferenceKmeansPDF(kde->kmeans, draw);
        for (j=0; j<kde->dimension; j++)
            fprintf(outp, "%g\t", draw[j]);
        fprintf(outp, "%g\n", PDF);
        XLALFree(draw);
    }
    fclose(outp);
}


/**
 * Add a KDE proposal to the KDE proposal set.
 *
 * If other KDE proposals already exist, the provided KDE is appended to the list, otherwise it is added
 * as the first of such proposals.
 * @param     propArgs The proposal arguments to be added to.
 * @param[in] kde      The proposal to be added to \a thread->cycle.
 */
void LALInferenceAddClusteredKDEProposalToSet(LALInferenceVariables *propArgs, LALInferenceClusteredKDE *kde) {
    /* If proposal doesn't already exist, add to proposal args */
    if (!LALInferenceCheckVariable(propArgs, clusteredKDEProposalName)) {
        LALInferenceAddVariable(propArgs, clusteredKDEProposalName, (void *)&kde, LALINFERENCE_void_ptr_t, LALINFERENCE_PARAM_LINEAR);

    /* If proposals already exist, add to the end */
    } else {
        LALInferenceClusteredKDE *existing_kde = *((LALInferenceClusteredKDE **)LALInferenceGetVariable(propArgs, clusteredKDEProposalName));
        LALInferenceClusteredKDE *old_kde = NULL;

        /* If the first proposal has the same name, replace it */
        if (!strcmp(existing_kde->name, kde->name)) {
            old_kde = existing_kde;
            kde->next = existing_kde->next;
            LALInferenceSetVariable(propArgs, clusteredKDEProposalName, (void *)&kde);
        } else {
            while (existing_kde->next != NULL) {
                /* Replace proposal with the same name if found */
                if (!strcmp(existing_kde->next->name, kde->name)) {
                    old_kde = existing_kde->next;
                    kde->next = old_kde->next;
                    existing_kde->next = kde;
                    break;
                }
                existing_kde = existing_kde->next;
            }

            /* If a proposal was not replaced, add the proposal to the end of the list */
            existing_kde->next=kde;
        }

        LALInferenceDestroyClusteredKDEProposal(old_kde);
    }

    return;
}


/**
 * Destroy an existing clustered-KDE proposal.
 *
 * Convenience function for freeing a clustered KDE proposal that
 * already exists.  This is particularly useful for a proposal that
 * is updated during a run.
 * @param proposal The proposal to be destroyed.
 */
void LALInferenceDestroyClusteredKDEProposal(LALInferenceClusteredKDE *proposal) {
    if (proposal != NULL) {
        LALInferenceClearVariables(proposal->params);
        LALInferenceKmeansDestroy(proposal->kmeans);
        XLALFree(proposal->params);
    }
    return;
}


/**
 * Setup a clustered-KDE proposal from the differential evolution buffer.
 *
 * Reads the samples currently in the differential evolution buffer and construct a
 * jump proposal from its clustered kernel density estimate.
 * @param thread The LALInferenceThreadState to get the buffer from and add the proposal to.
 */
void LALInferenceSetupClusteredKDEProposalFromDEBuffer(LALInferenceThreadState *thread) {
    INT4 i;

    /* If ACL can be estimated, thin DE buffer to only have independent samples */
    REAL8 bufferSize = (REAL8) thread->differentialPointsLength;
    REAL8 effSampleSize = (REAL8) LALInferenceComputeEffectiveSampleSize(thread);

    /* Correlations wont effect the proposal much, so floor is taken instead of ceil
     * when determining the step size */
    INT4 step = 1;
    if (effSampleSize > 0)
        step = (INT4) floor(bufferSize/effSampleSize);

    if (step == 0)
        step = 1;
    INT4 nPoints = (INT4) ceil(bufferSize/(REAL8)step);

    /* Get points to be clustered from the differential evolution buffer. */
    INT4 nPar = LALInferenceGetVariableDimensionNonFixed(thread->currentParams);
    REAL8** DEsamples = (REAL8**) XLALCalloc(nPoints, sizeof(REAL8*));
    REAL8*  temp = (REAL8*) XLALCalloc(nPoints * nPar, sizeof(REAL8));
    for (i=0; i < nPoints; i++)
      DEsamples[i] = temp + (i*nPar);

    LALInferenceThinnedBufferToArray(thread, DEsamples, step);

    /* Check if imposing cyclic reflective bounds */
    INT4 cyclic_reflective = LALInferenceGetINT4Variable(thread->proposalArgs, "cyclic_reflective_kde");

    INT4 ntrials = 5;
    LALInferenceSetupClusteredKDEProposalFromRun(thread, DEsamples[0], nPoints, cyclic_reflective, ntrials);

    /* The proposal copies the data, so the local array can be freed */
    XLALFree(temp);
    XLALFree(DEsamples);
}

/**
 * Setup a clustered-KDE proposal from the parameters in a run.
 *
 * Reads the samples currently in the differential evolution buffer and construct a
 * jump proposal from its clustered kernel density estimate.
 * @param runState The LALInferenceRunState to get the buffer from and add the proposal to.
 * @param samples  The samples to estimate the distribution of.  Column order expected to match
 *                     the order in \a runState->currentParams.
 * @param size     Number of samples in \a samples.
 * @param cyclic_reflective Flag to check for cyclic/reflective bounds.
 * @param ntrials  Number of tirals at fixed-k to find optimal BIC
 */
void LALInferenceSetupClusteredKDEProposalFromRun(LALInferenceThreadState *thread, REAL8 *samples, INT4 size, INT4 cyclic_reflective, INT4 ntrials) {
    REAL8 weight=2.;

    /* Keep track of clustered parameter names */
    LALInferenceVariables *backwardClusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));
    LALInferenceVariables *clusterParams = XLALCalloc(1, sizeof(LALInferenceVariables));
    LALInferenceVariableItem *item;
    for (item = thread->currentParams->head; item; item = item->next)
        if (LALInferenceCheckVariableNonFixed(thread->currentParams, item->name))
            LALInferenceAddVariable(backwardClusterParams, item->name, item->value, item->type, item->vary);
    for (item = backwardClusterParams->head; item; item = item->next)
        LALInferenceAddVariable(clusterParams, item->name, item->value, item->type, item->vary);

    /* Build the proposal */
    LALInferenceClusteredKDE *proposal = XLALCalloc(1, sizeof(LALInferenceClusteredKDE));
    LALInferenceInitClusteredKDEProposal(thread, proposal, samples, size, clusterParams, clusteredKDEProposalName, weight, LALInferenceOptimizedKmeans, cyclic_reflective, ntrials);

    /* Only add the kmeans was successfully setup */
    if (proposal->kmeans)
        LALInferenceAddClusteredKDEProposalToSet(thread->proposalArgs, proposal);
    else {
        LALInferenceClearVariables(clusterParams);
        XLALFree(clusterParams);
        XLALFree(proposal);
    }

    LALInferenceClearVariables(backwardClusterParams);
    XLALFree(backwardClusterParams);
}


/**
 * A proposal based on the clustered kernal density estimate of a set of samples.
 *
 * Proposes samples from the estimated distribution of a collection of points.
 * The distribution is estimated with a clustered kernel density estimator.  This
 * proposal is added to the proposal cycle with a specified weight, and in turn
 * chooses at random a KDE-estimate from a linked list.
 * @param      thread         The current LALInferenceThreadState.
 * @param      currentParams  The current parameters.
 * @param[out] proposedParams The proposed parameters.
 * @return proposal_ratio     The (log) proposal ratio for maintaining detailed balance
 */
REAL8 LALInferenceClusteredKDEProposal(LALInferenceThreadState *thread, LALInferenceVariables *currentParams, LALInferenceVariables *proposedParams) {
    REAL8 logPropRatio;

    logPropRatio = LALInferenceStoredClusteredKDEProposal(thread, currentParams, proposedParams, NULL);

    return logPropRatio;
}

/**
 * An interface to the KDE proposal that avoids a KDE evaluation if possible.
 *
 * If the value of the KDE at the current location is known, use it.  Otherwise
 * calculate and return.
 * @param      thread         The current LALInferenceThreadState.
 * @param      currentParams  The current parameters.
 * @param[out] proposedParams The proposed parameters.
 * @param      propDensity    If input is not NULL or >-DBL_MAX, assume this is the
 *                              proposal density at \a currentParams, otherwise
 *                              calculate.  It is then replaced with the proposal
 *                              density at \a proposedParams.
 * @return proposal_ratio    The (log) proposal ratio for maintaining detailed balance
 */
REAL8 LALInferenceStoredClusteredKDEProposal(LALInferenceThreadState *thread, LALInferenceVariables *currentParams, LALInferenceVariables *proposedParams, REAL8 *propDensity) {
    REAL8 cumulativeWeight, totalWeight;
    REAL8 logPropRatio = 0.0;

    LALInferenceVariableItem *item;
    LALInferenceVariables *propArgs = thread->proposalArgs;

    if (!LALInferenceCheckVariable(propArgs, clusteredKDEProposalName)) {
        LALInferenceClearVariables(proposedParams);
        return logPropRatio; /* Quit now, since there is no proposal to call */
    }

    LALInferenceCopyVariables(currentParams, proposedParams);

    /* Clustered KDE estimates are stored in a linked list, with possibly different weights */
    LALInferenceClusteredKDE *kdes = *((LALInferenceClusteredKDE **)LALInferenceGetVariable(propArgs, clusteredKDEProposalName));

    totalWeight = 0.;
    LALInferenceClusteredKDE *kde = kdes;
    while (kde!=NULL) {
        totalWeight += kde->weight;
        kde = kde->next;
    }

    /* If multiple KDE estimates exists, draw one at random */
    REAL8 randomDraw = gsl_rng_uniform(thread->GSLrandom);

    kde = kdes;
    cumulativeWeight = kde->weight;
    while(cumulativeWeight/totalWeight < randomDraw) {
        kde = kde->next;
        cumulativeWeight += kde->weight;
    }

    /* Draw a sample and fill the proposedParams variable with the parameters described by the KDE */
    REAL8 *current = XLALCalloc(kde->dimension, sizeof(REAL8));
    REAL8 *proposed = LALInferenceKmeansDraw(kde->kmeans);

    INT4 i=0;
    for (item = kde->params->head; item; item = item->next) {
        if (LALInferenceCheckVariableNonFixed(kde->params, item->name)) {
            current[i] = *(REAL8 *) LALInferenceGetVariable(currentParams, item->name);
            LALInferenceSetVariable(proposedParams, item->name, &(proposed[i]));
            i++;
        }
    }

    /* Calculate the proposal ratio */
    REAL8 logCurrentP;
    if (propDensity == NULL || *propDensity == -DBL_MAX)
        logCurrentP = LALInferenceKmeansPDF(kde->kmeans, current);
    else
        logCurrentP = *propDensity;

    REAL8 logProposedP = LALInferenceKmeansPDF(kde->kmeans, proposed);

    logPropRatio = logCurrentP - logProposedP;

    if (propDensity != NULL)
        *propDensity = logProposedP;

    XLALFree(current);
    XLALFree(proposed);

    return logPropRatio;
}


/**
 * A wrapper for the KDE proposal that doesn't store KDE evaluations.
 *
 */


/**
 * Compute the maximum ACL from the differential evolution buffer.
 *
 * Given the current differential evolution buffer, the maximum
 * one-dimensional autocorrelation length is found.
 * @param thread The thread state containing the differential evolution buffer.
 * @param maxACL UNDOCUMENTED
*/
void LALInferenceComputeMaxAutoCorrLenFromDE(LALInferenceThreadState *thread, INT4* maxACL) {
    INT4 nPar, nPoints;
    INT4 Nskip = 1;
    INT4 i;
    REAL8** DEarray;
    REAL8*  temp;
    REAL8 max_acl;

    nPar = LALInferenceGetVariableDimensionNonFixed(thread->currentParams);
    nPoints = thread->differentialPointsLength;

    /* Determine the number of iterations between each entry in the DE buffer */
    Nskip = LALInferenceGetINT4Variable(thread->proposalArgs, "Nskip");

    /* Prepare 2D array for DE points */
    DEarray = (REAL8**) XLALCalloc(nPoints, sizeof(REAL8*));
    temp = (REAL8*) XLALCalloc(nPoints * nPar, sizeof(REAL8));
    for (i=0; i < nPoints; i++)
        DEarray[i] = temp + (i*nPar);

    LALInferenceBufferToArray(thread, DEarray);
    max_acl = Nskip * LALInferenceComputeMaxAutoCorrLen(DEarray[nPoints/2], nPoints-nPoints/2, nPar);

    *maxACL = (INT4)max_acl;
    XLALFree(temp);
    XLALFree(DEarray);
}

/**
 * Compute the maximum single-parameter autocorrelation length.
 *
 * 1 + 2*ACF(1) + 2*ACF(2) + ... + 2*ACF(M*s) < s,
 *
 * the short length so that the sum of the ACF function
 * is smaller than that length over a window of M times
 * that length.
 *
 * The maximum window length is restricted to be N/K as
 * a safety precaution against relying on data near the
 * extreme of the lags in the ACF, where there is a lot
 * of noise.
 * @param array Array with rows containing samples.
 * @param nPoints UNDOCUMENTED
 * @param nPar UNDOCUMENTED
 * @return The maximum one-dimensional autocorrelation length
*/
REAL8 LALInferenceComputeMaxAutoCorrLen(REAL8 *array, INT4 nPoints, INT4 nPar) {
    INT4 M=5, K=2;

    REAL8 mean, ACL, ACF, maxACL=0;
    INT4 par=0, lag=0, i=0, imax;
    REAL8 cumACF, s;

    if (nPoints > 1) {
        imax = nPoints/K;

        for (par=0; par<nPar; par++) {
            mean = gsl_stats_mean(array+par, nPar, nPoints);
            for (i=0; i<nPoints; i++)
                array[i*nPar + par] -= mean;

            lag=1;
            ACL=1.0;
            ACF=1.0;
            s=1.0;
            cumACF=1.0;
            while (cumACF >= s) {
                ACF = gsl_stats_correlation(array + par, nPar, array + lag*nPar + par, nPar, nPoints-lag);
                cumACF += 2.0 * ACF;
                lag++;
                s = (REAL8)lag/(REAL8)M;
                if (lag > imax) {
                    ACL = INFINITY;
                    break;
                }
            }
            ACL = s;
            if (ACL>maxACL)
                maxACL=ACL;

            for (i=0; i<nPoints; i++)
                array[i*nPar + par] += mean;
        }
    } else {
        maxACL = INFINITY;
    }

    return maxACL;
}

/**
 * Update the estimatate of the autocorrelation length.
 *
 * @param      thread      The current LALInferenceThreadState.
*/
void LALInferenceUpdateMaxAutoCorrLen(LALInferenceThreadState *thread) {
  // Calculate ACL with latter half of data to avoid ACL overestimation from chain equilibrating after adaptation
  INT4 acl;

  LALInferenceComputeMaxAutoCorrLenFromDE(thread, &acl);
  LALInferenceSetVariable(thread->proposalArgs, "acl", &acl);
}

/**
 * Determine the effective sample size based on the DE buffer.
 *
 * Compute the number of independent samples in the differential evolution
 * buffer.
 * @param      thread      The current LALInferenceThreadState.
 */
INT4 LALInferenceComputeEffectiveSampleSize(LALInferenceThreadState *thread) {
    /* Update the ACL estimate, assuming a thinned DE buffer if ACL isn't available */
    INT4 acl = 1;
    if (LALInferenceCheckVariable(thread->proposalArgs, "acl")) {
        LALInferenceUpdateMaxAutoCorrLen(thread);
        acl = LALInferenceGetINT4Variable(thread->proposalArgs, "acl");
    }

    /* Estimate the total number of samples post-burnin based on samples in DE buffer */
    INT4 nPoints =  thread->differentialPointsLength * thread->differentialPointsSkip;
    INT4 iEff = nPoints/acl;
    return iEff;
}


INT4 LALInferencePrintProposalTrackingHeader(FILE *fp,LALInferenceVariables *params) {
      fprintf(fp, "proposal\t");
      LALInferenceFprintParameterNonFixedHeaders(fp, params);
      LALInferenceFprintParameterNonFixedHeadersWithSuffix(fp, params, "p");
      fprintf(fp, "prop_ratio\taccepted\t");
      fprintf(fp, "\n");
      return 0;
}

void LALInferencePrintProposalTracking(FILE *fp, LALInferenceProposalCycle *cycle, LALInferenceVariables *theta, LALInferenceVariables *theta_prime, REAL8 logPropRatio, INT4 accepted){
  fprintf(fp, "%s\t", cycle->proposals[cycle->counter]->name);
  LALInferencePrintSampleNonFixed(fp, theta);
  LALInferencePrintSampleNonFixed(fp, theta_prime);
  fprintf(fp, "%9.5f\t", exp(logPropRatio));
  fprintf(fp, "%d\t", accepted);
  fprintf(fp, "\n");
  return;
}

REAL8 LALInferenceSplineCalibrationProposal(LALInferenceThreadState *thread, LALInferenceVariables *currentParams, LALInferenceVariables *proposedParams) {
  const char *proposalName = splineCalibrationProposalName;
  char **ifo_names;
  INT4 ifo;
  INT4 nifo = LALInferenceGetINT4Variable(thread->proposalArgs, "nDet");
  REAL8 ampWidth = *(REAL8 *)LALInferenceGetVariable(thread->priorArgs, "spcal_amp_uncertainty");
  REAL8 phaseWidth = *(REAL8 *)LALInferenceGetVariable(thread->priorArgs, "spcal_phase_uncertainty");

  LALInferenceCopyVariables(currentParams, proposedParams);
  LALInferenceSetVariable(thread->proposalArgs, LALInferenceCurrentProposalName, &proposalName);

  ifo_names = *(char ***)LALInferenceGetVariable(thread->proposalArgs, "detector_names");
  for (ifo=0; ifo<nifo; ifo++) {
    size_t i;

    char ampName[VARNAME_MAX];
    char phaseName[VARNAME_MAX];

    REAL8Vector *amps;
    REAL8Vector *phases;

    snprintf(ampName, VARNAME_MAX, "%s_spcal_amp", ifo_names[ifo]);
    snprintf(phaseName, VARNAME_MAX, "%s_spcal_phase", ifo_names[ifo]);

    amps = *(REAL8Vector **)LALInferenceGetVariable(proposedParams, ampName);
    phases = *(REAL8Vector **)LALInferenceGetVariable(proposedParams, phaseName);

    for (i = 0; i < amps->length; i++) {
      amps->data[i] += ampWidth*gsl_ran_ugaussian(thread->GSLrandom)/sqrt(nifo*amps->length);
      phases->data[i] += phaseWidth*gsl_ran_ugaussian(thread->GSLrandom)/sqrt(nifo*amps->length);
    }
  };

  return 0.0;
}

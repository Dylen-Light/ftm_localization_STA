/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 * File: _coder_trilateration_api.h
 *
 * MATLAB Coder version            : 23.2
 * C/C++ source code generated on  : 14-Mar-2024 14:32:04
 */

#ifndef _CODER_TRILATERATION_API_H
#define _CODER_TRILATERATION_API_H

/* Include Files */
#include "emlrt.h"
#include "mex.h"
#include "tmwtypes.h"
#include <string.h>

/* Variable Declarations */
extern emlrtCTX emlrtRootTLSGlobal;
extern emlrtContext emlrtContextGlobal;

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
void trilateration(real_T nodeNumber, real_T nodeList_data[],
                   int32_T nodeList_size[2], real_T disList_data[],
                   int32_T disList_size[2], real_T X[2]);

void trilateration_api(const mxArray *const prhs[3], const mxArray **plhs);

void trilateration_atexit(void);

void trilateration_initialize(void);

void trilateration_terminate(void);

void trilateration_xil_shutdown(void);

void trilateration_xil_terminate(void);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for _coder_trilateration_api.h
 *
 * [EOF]
 */

// Copyright 2014-2015 Isis Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../../Utils/ITMPixelUtils.h"

template<bool shortIteration, bool rotationOnly, bool useWeights>
_CPU_AND_GPU_CODE_ inline bool computePerPointGH_exDepth_Ab(THREADPTR(float) *A, THREADPTR(float) &b,
	const THREADPTR(int) & x, const THREADPTR(int) & y, const CONSTPTR(float) &depth, THREADPTR(float) &depthWeight,
	const CONSTPTR(Vector2i) & viewImageSize, const CONSTPTR(Vector4f) & viewIntrinsics, const CONSTPTR(Vector2i) & sceneImageSize,
	const CONSTPTR(Vector4f) & sceneIntrinsics, const CONSTPTR(Matrix4f) & approxInvPose, const CONSTPTR(Matrix4f) & scenePose, const CONSTPTR(Vector4f) *pointsMap,
	const CONSTPTR(Vector4f) *normalsMap, float spaceThresh, float viewFrustum_min, float viewFrustum_max, int tukeyCutOff, int framesToSkip, int framesToWeight)
{
	depthWeight = 0;

	if (depth <= 1e-8f) return false; //check if valid -- != 0.0f

	Vector4f tmp3Dpoint, tmp3Dpoint_reproj; Vector3f ptDiff;
	Vector4f curr3Dpoint, corr3Dnormal; Vector2f tmp2Dpoint;

	tmp3Dpoint.x = depth * ((float(x) - viewIntrinsics.z) / viewIntrinsics.x);
	tmp3Dpoint.y = depth * ((float(y) - viewIntrinsics.w) / viewIntrinsics.y);
	tmp3Dpoint.z = depth;
	tmp3Dpoint.w = 1.0f;

	// transform to previous frame coordinates
	tmp3Dpoint = approxInvPose * tmp3Dpoint;
	tmp3Dpoint.w = 1.0f;

	// project into previous rendered image
	tmp3Dpoint_reproj = scenePose * tmp3Dpoint;
	if (tmp3Dpoint_reproj.z <= 0.0f) return false;
	tmp2Dpoint.x = sceneIntrinsics.x * tmp3Dpoint_reproj.x / tmp3Dpoint_reproj.z + sceneIntrinsics.z;
	tmp2Dpoint.y = sceneIntrinsics.y * tmp3Dpoint_reproj.y / tmp3Dpoint_reproj.z + sceneIntrinsics.w;

	if (!((tmp2Dpoint.x >= 0.0f) && (tmp2Dpoint.x <= sceneImageSize.x - 2) && (tmp2Dpoint.y >= 0.0f) && (tmp2Dpoint.y <= sceneImageSize.y - 2)))
		return false;

	curr3Dpoint = interpolateBilinear_withHoles(pointsMap, tmp2Dpoint, sceneImageSize);
	if (curr3Dpoint.w < 0.0f) return false;
	
	ptDiff.x = curr3Dpoint.x - tmp3Dpoint.x;
	ptDiff.y = curr3Dpoint.y - tmp3Dpoint.y;
	ptDiff.z = curr3Dpoint.z - tmp3Dpoint.z;
	float dist = ptDiff.x * ptDiff.x + ptDiff.y * ptDiff.y + ptDiff.z * ptDiff.z;

	if (dist > tukeyCutOff * spaceThresh) return false;

	corr3Dnormal = interpolateBilinear_withHoles(normalsMap, tmp2Dpoint, sceneImageSize);
	//if (corr3Dnormal.w < 0.0f) return false;

	depthWeight = MAX(0.0f, 1.0f - (depth - viewFrustum_min) / (viewFrustum_max - viewFrustum_min));
	depthWeight *= depthWeight;

	if (useWeights)
	{
		if (curr3Dpoint.w < framesToSkip) return false;
		depthWeight *= (curr3Dpoint.w - framesToSkip) / framesToWeight;
	}

	b = corr3Dnormal.x * ptDiff.x + corr3Dnormal.y * ptDiff.y + corr3Dnormal.z * ptDiff.z;

	// TODO check whether normal matches normal from image, done in the original paper, but does not seem to be required
	if (shortIteration)
	{
		if (rotationOnly)
		{
			A[0] = +tmp3Dpoint.z * corr3Dnormal.y - tmp3Dpoint.y * corr3Dnormal.z;
			A[1] = -tmp3Dpoint.z * corr3Dnormal.x + tmp3Dpoint.x * corr3Dnormal.z;
			A[2] = +tmp3Dpoint.y * corr3Dnormal.x - tmp3Dpoint.x * corr3Dnormal.y;
		}
		else { A[0] = corr3Dnormal.x; A[1] = corr3Dnormal.y; A[2] = corr3Dnormal.z; }
	}
	else
	{
		A[0] = +tmp3Dpoint.z * corr3Dnormal.y - tmp3Dpoint.y * corr3Dnormal.z;
		A[1] = -tmp3Dpoint.z * corr3Dnormal.x + tmp3Dpoint.x * corr3Dnormal.z;
		A[2] = +tmp3Dpoint.y * corr3Dnormal.x - tmp3Dpoint.x * corr3Dnormal.y;
		A[!shortIteration ? 3 : 0] = corr3Dnormal.x; A[!shortIteration ? 4 : 1] = corr3Dnormal.y; A[!shortIteration ? 5 : 2] = corr3Dnormal.z;
	}

	return true;
}

_CPU_AND_GPU_CODE_ inline bool computePerPointGH_exRGB_Ab(THREADPTR(float) *localGradient, THREADPTR(float) &colourDifferenceSq, THREADPTR(float) *localHessian,
	THREADPTR(Vector4f) pt_model, const THREADPTR(Vector4u) *rgb_model, DEVICEPTR(Vector4u) *rgb_live, const CONSTPTR(Vector2i) & imgSize,
	int x, int y, Vector4f projParams, Matrix4f approxPose, Matrix4f approxInvPose, Matrix4f scenePose, DEVICEPTR(Vector4s) *gx, DEVICEPTR(Vector4s) *gy, int numPara)
{
	Vector4f pt_camera, colour_model, colour_obs, gx_obs, gy_obs;
	Vector3f colour_diff_d, d_pt_cam_dpi, d[6];
	Vector2f pt_image_live, pt_image_model, d_proj_dpi;

	if (pt_model.w <= 0.f) return false;

	pt_model.w = 1.f;
	pt_camera = approxPose * pt_model; // convert the point in camera coordinates using the candidate pose

	if (pt_camera.z <= 0) return false;

	// project the point onto the live image
	pt_image_live.x = projParams.x * pt_camera.x / pt_camera.z + projParams.z;
	pt_image_live.y = projParams.y * pt_camera.y / pt_camera.z + projParams.w;

	if (pt_image_live.x < 1 || pt_image_live.x > imgSize.x - 2 || pt_image_live.y < 1 || pt_image_live.y > imgSize.y - 2) return false;

	// convert the point in model coordinates
	Vector4f pt_model_reproj = scenePose * pt_model;

	// Project the point onto the previous frame
	pt_image_model.x = projParams.x * pt_model_reproj.x / pt_model_reproj.z + projParams.z;
	pt_image_model.y = projParams.y * pt_model_reproj.y / pt_model_reproj.z + projParams.w;

	if (pt_image_model.x < 1 || pt_image_model.x > imgSize.x - 2 || pt_image_model.y < 1 || pt_image_model.y > imgSize.y - 2) return false;

	colour_model = interpolateBilinear(rgb_model, pt_image_model, imgSize) / 255.f;
	colour_obs = interpolateBilinear(rgb_live, pt_image_live, imgSize) / 255.f;
	gx_obs = interpolateBilinear(gx, pt_image_live, imgSize) / 255.f; // gx and gy are computed from the live image
	gy_obs = interpolateBilinear(gy, pt_image_live, imgSize) / 255.f;

	if (colour_obs.w <= 1e-7f || colour_model.w <= 1e-7f) return false;

	colour_diff_d.x = colour_obs.x - colour_model.x;
	colour_diff_d.y = colour_obs.y - colour_model.y;
	colour_diff_d.z = colour_obs.z - colour_model.z;

	colourDifferenceSq = colour_diff_d.x * colour_diff_d.x + colour_diff_d.y * colour_diff_d.y + colour_diff_d.z * colour_diff_d.z;

	// Derivatives computed as in
	// Blanco, J. (2010). A tutorial on se (3) transformation parameterizations and on-manifold optimization.
	// University of Malaga, Tech. Rep
	// Equation A.13

	Vector3f d_proj_const_x, d_proj_const_y;

	d_proj_const_x.x = projParams.x / pt_camera.z;
	d_proj_const_x.y = 0.f;
	d_proj_const_x.z = -projParams.x * pt_camera.x / (pt_camera.z * pt_camera.z);

	d_proj_const_y.x = 0.f;
	d_proj_const_y.y = projParams.y / pt_camera.z;
	d_proj_const_y.z = -projParams.y * pt_camera.y / (pt_camera.z * pt_camera.z);

	for (int para = 0, counter = 0; para < numPara; para++)
	{
//		switch (para)
//		{
//		case 0: //rx
//			d_proj_dpi.x = -projParams.x * pt_camera.y * pt_camera.x / (pt_camera.z * pt_camera.z);
//			d_proj_dpi.y = -projParams.y * (pt_camera.z * pt_camera.z + pt_camera.y * pt_camera.y) / (pt_camera.z * pt_camera.z);
//			break;
//		case 1: // ry
//			d_proj_dpi.x = projParams.x * (pt_camera.z * pt_camera.z + pt_camera.x * pt_camera.x) / (pt_camera.z * pt_camera.z);
//			d_proj_dpi.y = projParams.y * pt_camera.x * pt_camera.y / (pt_camera.z * pt_camera.z);
//			break;
//		case 2: // rz
//			d_proj_dpi.x = -projParams.x * pt_camera.y / pt_camera.z;
//			d_proj_dpi.y = projParams.y * pt_camera.x / pt_camera.z;
//			break; //rz
//		case 3: //tx
//			d_proj_dpi.x = projParams.x / pt_camera.z;
//			d_proj_dpi.y = 0.0f;
//			break;
//		case 4: //ty
//			d_proj_dpi.x = 0.0f;
//			d_proj_dpi.y = projParams.y / pt_camera.z;
//			break;
//		case 5: //tz
//			d_proj_dpi.x = -projParams.x * pt_camera.x / (pt_camera.z * pt_camera.z);
//			d_proj_dpi.y = -projParams.y * pt_camera.y / (pt_camera.z * pt_camera.z);
//			break;
//		};

		Vector3f d_point_col;

		switch (para)
		{
		case 0: //rx
			d_point_col.x = approxInvPose.m01 * pt_model.z - approxInvPose.m02 * pt_model.y;
			d_point_col.y = approxInvPose.m11 * pt_model.z - approxInvPose.m12 * pt_model.y;
			d_point_col.z = approxInvPose.m21 * pt_model.z - approxInvPose.m22 * pt_model.y;
			break;
		case 1: // ry
			d_point_col.x = approxInvPose.m02 * pt_model.x - approxInvPose.m00 * pt_model.z;
			d_point_col.y = approxInvPose.m12 * pt_model.x - approxInvPose.m10 * pt_model.z;
			d_point_col.z = approxInvPose.m22 * pt_model.x - approxInvPose.m20 * pt_model.z;
			break;
		case 2: // rz
			d_point_col.x = approxInvPose.m00 * pt_model.y - approxInvPose.m01 * pt_model.x;
			d_point_col.y = approxInvPose.m10 * pt_model.y - approxInvPose.m11 * pt_model.x;
			d_point_col.z = approxInvPose.m20 * pt_model.y - approxInvPose.m21 * pt_model.x;
			break; //rz
		case 3: //tx
			// Rotation matrix negated and transposed (matrix storage is column major, though)
			// We negate it one more time (-> no negation) because the ApplyDelta uses the KinectFusion
			// skew symmetric matrix, that matrix has negated rotation components.
			// In order to use the rgb tracker we would need to negate the entire computed step, but given
			// the peculiar structure of the increment matrix we only need to negate the translation component.
			d_point_col.x = approxInvPose.m00;
			d_point_col.y = approxInvPose.m10;
			d_point_col.z = approxInvPose.m20;
			break;
		case 4: //ty
			d_point_col.x = approxInvPose.m01;
			d_point_col.y = approxInvPose.m11;
			d_point_col.z = approxInvPose.m21;
			break;
		case 5: //tz
			d_point_col.x = approxInvPose.m02;
			d_point_col.y = approxInvPose.m12;
			d_point_col.z = approxInvPose.m22;
			break;
		};

		d_proj_dpi.x = dot(d_proj_const_x, d_point_col);
		d_proj_dpi.y = dot(d_proj_const_y, d_point_col);

		d[para].x = d_proj_dpi.x * gx_obs.x + d_proj_dpi.y * gy_obs.x;
		d[para].y = d_proj_dpi.x * gx_obs.y + d_proj_dpi.y * gy_obs.y;
		d[para].z = d_proj_dpi.x * gx_obs.z + d_proj_dpi.y * gy_obs.z;

		localGradient[para] = 2.0f * (d[para].x * colour_diff_d.x + d[para].y * colour_diff_d.y + d[para].z * colour_diff_d.z);

		for (int col = 0; col <= para; col++)
			localHessian[counter++] = 2.0f * (d[para].x * d[col].x + d[para].y * d[col].y + d[para].z * d[col].z);
	}

	return true;
}

template<bool shortIteration, bool rotationOnly, bool useWeights>
_CPU_AND_GPU_CODE_ inline bool computePerPointGH_exDepth(THREADPTR(float) *localNabla, THREADPTR(float) *localHessian, THREADPTR(float) &localF,
	const THREADPTR(int) & x, const THREADPTR(int) & y, const CONSTPTR(float) &depth, THREADPTR(float) &depthWeight, CONSTPTR(Vector2i) & viewImageSize, const CONSTPTR(Vector4f) & viewIntrinsics, 
	const CONSTPTR(Vector2i) & sceneImageSize, const CONSTPTR(Vector4f) & sceneIntrinsics, const CONSTPTR(Matrix4f) & approxInvPose, const CONSTPTR(Matrix4f) & scenePose, 
	const CONSTPTR(Vector4f) *pointsMap, const CONSTPTR(Vector4f) *normalsMap, float spaceThreash, float viewFrustum_min, float viewFrustum_max, int tukeyCutOff, int framesToSkip, int framesToWeight)
{
	const int noPara = shortIteration ? 3 : 6;
	float A[noPara];
	float b;

	bool ret = computePerPointGH_exDepth_Ab<shortIteration, rotationOnly, useWeights>(A, b, x, y, depth, depthWeight, viewImageSize, viewIntrinsics, sceneImageSize, sceneIntrinsics,
		approxInvPose, scenePose, pointsMap, normalsMap, spaceThreash, viewFrustum_min, viewFrustum_max, tukeyCutOff, framesToSkip, framesToWeight);

	if (!ret) return false;

	localF = b * b;

#if (defined(__CUDACC__) && defined(__CUDA_ARCH__)) || (defined(__METALC__))
#pragma unroll
#endif
	for (int r = 0, counter = 0; r < noPara; r++)
	{
		localNabla[r] = b * A[r];
#if (defined(__CUDACC__) && defined(__CUDA_ARCH__)) || (defined(__METALC__))
#pragma unroll
#endif
		for (int c = 0; c <= r; c++, counter++) localHessian[counter] = A[r] * A[c];
	}

	return true;
}


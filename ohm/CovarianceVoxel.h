// Copyright (c) 2020
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#ifndef COVARIANCEVOXEL_H
#define COVARIANCEVOXEL_H

// Note: this header is included in GPU code.
// Because of this "OhmConfig.h", <glm>, <cmath> cannot be included here and you may need to include the following
// headers first:
// #define GLM_ENABLE_EXPERIMENTAL
// <glm/vec3.hpp>
// <glm/mat3x3.hpp>
// <glm/gtx/norm.hpp>

// #if GPUTIL_DEVICE
// #ifndef __device__
// #define __device__
// #endif  // __device__
// #ifndef __host__
// #define __host__
// #endif  // __host__
// #endif  // GPUTIL_DEVICE

#if GPUTIL_DEVICE
// Define GPU type aliases
typedef float3 covvec3;
typedef float covreal;
typedef uint uint32_t;

// Vector support functions
inline __device__ covreal covdot(const covvec3 a, const covvec3 b)
{
  return dot(a, b);
}
inline __device__ covreal covlength2(const covvec3 v)
{
  return dot(v, v);
}
inline __device__ covvec3 covnormalize(const covvec3 v)
{
  return normalize(v);
}

#else  // GPUTIL_DEVICE
namespace ohm
{
  // Define CPU type aliases
  using covvec3 = glm::dvec3;
  using covreal = double;


  inline covreal covdot(const covvec3 &a, const covvec3 &b) { return glm::dot(a, b); }
  inline covreal covlength2(const covvec3 &v) { return glm::length2(v); }
  inline covvec3 covnormalize(const covvec3 &v) { return glm::normalize(v); }
#ifndef __device__
#define __device__
#endif  //  __device__
#endif  // GPUTIL_DEVICE
typedef struct CovarianceVoxel_t
{
  /// Sparse covariance matrix. See @c unpackCovariance() for details.
  float trianglar_covariance[6];
} CovarianceVoxel;


/// Initialise the packed covariance matrix in @p cov
/// The covariance value is initialised to an identity matrix, scaled by the @p sensor_noise squared.
/// @param[out] cov The @c CovarianceVoxel to initialse.
/// @param sensor_noise The sensor range noise error. Must be greater than zero.
inline __device__ void initialiseCovariance(CovarianceVoxel *cov, float sensor_noise)
{
  // Initialise the covariance matrix to a scaled identity matrix based on the sensor noise.
  cov->trianglar_covariance[0] = cov->trianglar_covariance[2] = cov->trianglar_covariance[5] =
    sensor_noise * sensor_noise;
  cov->trianglar_covariance[1] = cov->trianglar_covariance[3] = cov->trianglar_covariance[4] = 0;
}


/// dot product of j-th and k-th columns of A
/// A is (4,3), assumed to be packed as follows, where z is non-represented zero
/// 0 1 3
/// z 2 4
/// z z 5
/// 6 7 8
inline __device__ double packedDot(const covreal A[9], const int j, const int k)
{
  const int col_first_el[] = { 0, 1, 3 };
  const int indj = col_first_el[j];
  const int indk = col_first_el[k];
  const int m = (j <= k) ? j : k;
  covreal d = A[6 + k] * A[6 + j];
  for (int i = 0; i <= m; ++i)
  {
    d += A[indj + i] * A[indk + i];
  }
  return d;
}


/// Unpack the covariance matrix storage.
///
/// The unpacked covariance matrix represents a sparse 3,4 matrix of the following form:
///
/// |         |         |         |
/// | ------- | ------- | ------- |
/// | cov[0]  | cov[1]  | cov[3]  |
/// | .       | cov[2]  | cov[4]  |
/// | .       | .       | cov[5]  |
/// | mean[0] | mean[1] | mean[2] |
///
/// Items marked `cov[n]` are extracted from the @c cov->trianglar_covariance, while `mean[n]` items are derived from
/// @p sample_to_mean . Items marked '.' are not represented in the martix and are treated as zero.
/// Note that the extracted values also have a co-efficient applied based on the @p point_count .
///
/// @param matrix The matrix to unpack to. This is an array of 9 elements.
/// @param sample_to_mean The difference between the new sample point and the voxel mean.
inline __device__ void unpackCovariance(const CovarianceVoxel *cov, unsigned point_count, const covvec3 sample_to_mean,
                                        covreal *matrix)
{
  const covreal one_on_num_pt_plus_one = (covreal)1 / (point_count + (covreal)1);
  const covreal sc_1 = point_count ? sqrt(point_count * one_on_num_pt_plus_one) : (covreal)1;
  const covreal sc_2 = one_on_num_pt_plus_one * sqrt((covreal)point_count);

  for (int i = 0; i < 6; ++i)
  {
    matrix[i] = sc_1 * cov->trianglar_covariance[i];
  }

  matrix[0 + 6] = sc_2 * sample_to_mean.x;
  matrix[1 + 6] = sc_2 * sample_to_mean.y;
  matrix[2 + 6] = sc_2 * sample_to_mean.z;
}

// Find x for Mx = y, given lower triangular M where M is @c trianglar_covariance
// Storage order for M:
// 0 z z
// 1 2 z
// 3 4 5
inline __device__ covvec3 solveTriangular(const CovarianceVoxel *cov, const covvec3 y)
{
  // Note: if we generate the voxel with point on a perfect plane, say (0, 0, 1, 0), then do this operation,
  // we get a divide by zero. We avoid this by seeding the covariance matrix with an identity matrix scaled
  // by the sensor noise (see initialiseCovariance()).
  covvec3 x;
  covreal d;

  d = y.x;
  x.x = d / cov->trianglar_covariance[0];

  d = y.y;
  d -= cov->trianglar_covariance[1 + 0] * x.x;
  x.y = d / cov->trianglar_covariance[1 + 1];

  d = y.z;
  d -= cov->trianglar_covariance[3 + 0] * x.x;
  d -= cov->trianglar_covariance[3 + 1] * x.y;
  x.z = d / cov->trianglar_covariance[3 + 2];

  return x;
}

/// Calculate a voxel hit with packed covariance. This supports Normalised Distribution Transform (NDT) logic in @c
/// calculateMissNdt() .
///
/// The covariance in @p cov_voxel and occupancy in @p voxel_value are both updated, but the voxel mean calculation
/// is not performed here. However, it is expected that the voxel mean will be updated after this call and the
/// @c point_count incremented, otherwise future behaviour is undefined.
///
/// The @p cov_voxel may be zero and is fully initialised when the @p point_count is zero, implying this is the first
/// hit. It will also be reinitialised whenever the @p voxel_value is below the the @p reinitialise_threshold and the
/// @p point_count is above @p reinitialise_sample_count .
///
/// This reinitialisation is to handle sitautions where a voxel may have been occupied by a transient object, become
/// free, then becomes occupied once more. In this case, the new occupancy covariance may differ and should disregard
/// the previous covariance and mean. The @c reinitialise_threshold is used a the primary trigger to indicate previous
/// data may be invalid while the @c reinitialise_sample_count is intended to prevent repeated reintialisation as the
/// probablity value may oscillate around the threshold.
///
/// @param[in,out] cov_voxel The packed covariance to update for the voxel being updated.
/// @param[in,out] voxel_value The occupancy value for the voxel being updated.
/// @param sample The sample which falls in this voxel.
/// @param voxel_mean The current accumulated mean position within the voxel. Only valid if @p point_count is &gt; 0.
/// @param point_count The number of samples which have been used to generate the @p voxel_mean and @p cov_voxel.
/// @param hit_value The log probably value increase for occupancy on a hit. This must be greater than zero to increase
/// the voxel occupancy probability.
/// @param uninitialised_value The @p voxel_value for an uncertain voxel - one which has yet to be observed.
/// @param sensor_noise The sensor range noise error. Must be greater than zero.
/// @param reinitialise_threshold @p voxel_value threshold below which the covariance and mean should reset.
/// @param reinitialise_sample_count The @p point_count requires to allow @c reinitialise_threshold to be triggered.
/// @return True if the covariance value is re-initialised. This should be used as a signal to diregard the current
///     @p voxel_mean and @c point_count and restart accumulating those values.
inline __device__ bool calculateHitWithCovariance(CovarianceVoxel *cov_voxel, float *voxel_value, covvec3 sample,
                                                  covvec3 voxel_mean, unsigned point_count, float hit_value,
                                                  float uninitialised_value, float sensor_noise,
                                                  float reinitialise_threshold, unsigned reinitialise_sample_count)
{
  const float initial_value = *voxel_value;
  const bool was_uncertain = initial_value == uninitialised_value;
  bool initialised_covariance = false;
  // Initialise the cov_voxel data if this transitions the voxel to an occupied state.
  if (was_uncertain || point_count == 0 ||
      initial_value < reinitialise_threshold && point_count >= reinitialise_sample_count)
  {
    // Transitioned to occupied. Initialise.
    initialiseCovariance(cov_voxel, sensor_noise);
    *voxel_value = hit_value;
    initialised_covariance = true;
  }
  else
  {
    *voxel_value += hit_value;
  }

  // This has been taken from example code provided by Jason Williams as a sample on storing and using covarance data
  // using a packed, diagonal.
  // Code represents covariance via square root matrix, i.e., covariance P = C * C^T
  // Let old covariance be P, new covariance Pnew, old mean mu, new point z
  // The required update for the covariance is
  //   Pnew = num_pt/(num_pt + 1)*P + num_pt/(num_pt+1)^2 * (z-mu)(z-mu)^T
  // This code implements that update directly via a matrix square root by forming the matrix A
  // such that A^T A = Pnew. A is not square, so a modified Gram-Schmidt decomposition is utilised
  // to find the triangular square root matrix Cnew such that Pnew = Cnew Cnew^T
  // Reference: Maybeck 1978 Stochastic Models, Estimation and Control, vol 1, p381
  // https://www.sciencedirect.com/bookseries/mathematics-in-science-and-engineering/vol/141/part/P1

  const covvec3 sample_to_mean = sample - voxel_mean;
  covreal unpacked_covariance[9];
  unpackCovariance(cov_voxel, point_count, sample_to_mean, unpacked_covariance);

  // Update covariance.
  for (int k = 0; k < 3; ++k)
  {
    const int ind1 = (k * (k + 3)) >> 1;  // packed index of (k,k) term
    const int indk = ind1 - k;            // packed index of (1,k)
    const covreal ak = sqrt(packedDot(unpacked_covariance, k, k));
    cov_voxel->trianglar_covariance[ind1] = (float)ak;
    if (ak > 0)
    {
      const covreal aki = (covreal)1 / ak;
      for (int j = k + 1; j < 3; ++j)
      {
        const int indj = (j * (j + 1)) >> 1;
        const int indkj = indj + k;
        covreal c = packedDot(unpacked_covariance, j, k) * aki;
        cov_voxel->trianglar_covariance[indkj] = (float)c;
        c *= aki;
        unpacked_covariance[j + 6] -= c * unpacked_covariance[k + 6];
        for (int l = 0; l <= k; ++l)
        {
          unpacked_covariance[indj + l] -= c * unpacked_covariance[indk + l];
        }
      }
    }
  }

  return initialised_covariance;
}

/// Calculate a voxel miss (ray passthrough) using Normalised Distribution Transform (NDT) logic.
///
/// This algorithm is based on the following paper:
/// > 3D normal distributions transform occupancy maps: An efficient representation for mapping in dynamic
/// > environments
/// > Jari P. Saarinen, Henrik Andreasson, Todor Stoyanov and Achim J. Lilienthal
///
/// This improves the probably adjustment for a voxel using the voxel covaraince (if present). This only takes effect
/// when there have been samples collected for the voxel and `point_count &gt 0`. The standard occupancy adjustment
/// is used whenever the `point_count &lt; sample_threshold`, with @p miss_value added to @p voxel_value or
/// @p voxel_value set to @p miss_value when @p voxel_value equals @p uninitialised_value .
///
/// @param cov_voxel The packed covariance for the voxel. Only used if the `point_count &gt; sample_threshold`.
/// @param[in,out] voxel_value The current voxel log probably value.
/// @param sensor The location of the sensor from where the sample was taken.
/// @param sample The sample position.
/// @param voxel_mean The current accumulated mean position within the voxel. Only valid if @p point_count is &gt; 0.
/// @param point_count The number of samples which have been used to generate the @p voxel_mean and @p cov_voxel.
/// @param uninitialised_value The @p voxel_value for an uncertain voxel - one which has yet to be observed.
/// @param miss_value The @p voxel_value adjustment to apply from a miss. This must be less than zero to decrease the
/// occupancy probability. The NDT scaling factor is also derived from this value.
/// @param sensor_noise The sensor range noise error. Must be greater than zero.
/// @param sample_threshold The @p point_count required before using NDT logic, i.e., before the covariance value is
/// usable.
inline __device__ covvec3 calculateMissNdt(const CovarianceVoxel *cov_voxel, float *voxel_value, covvec3 sensor,
                                           covvec3 sample, covvec3 voxel_mean, unsigned point_count,
                                           float uninitialised_value, float miss_value, float sensor_noise,
                                           unsigned sample_threshold)
{
  if (*voxel_value == uninitialised_value)
  {
    // First touch of the voxel. Apply the miss value as is.
    // Same behaviour as OccupancyMap.
    *voxel_value = miss_value;
    return voxel_mean;
  }

  // Direct value adjustment if not occupied or insufficient samples.
  if (point_count < sample_threshold)
  {
    // Re-enforcement of free voxel or too few points to resolve a guassing. Use standard value update.
    // Add miss value, same behaviour as OccupancyMap.
    *voxel_value += miss_value;
    return voxel_mean;
  }

  // Update of an occupied voxel. We have to unpack the covariance and apply NDT logic.

  // Notes:
  // - Equation references are in relation to the paper on which this is based (see class comments).
  // - Variable subscripts are denoted by '_<subscript>'; e.g., "z subscript i" is written "z_i".
  // - A transpose is donoted by [T]
  // - Ordinals are denoted by [#]; e.g.,
  //    - [-1] -> inverse
  //    - [2] -> square
  // - The paper used capital Sigma for the covariance matrix. We use P.
  //
  // Goal is to calculate equation (24)
  // p(m_k = 1|z_i) = 0.5 - np(x_ML|N(u,P)) (1 - p(x_ML|z_i))      (24)
  // We have already established we have sufficient points for a gaussian.

  // p(x_ML|N(u,P)) ~ exp( -0.5(x_ML - u)[T] P[-1](x_ML - u))     (22)
  // Where know values are:
  //  - u existing mean voxel position (voxel mean position)
  //  - P is the covariance matrix.
  //  - z_i is the sample
  // To be calcualated:
  // - x_ML

  // p(x_ML|z_i) ~ exp( -0.5 || x_ML - z_i ||[2] / s_s[2] )       (23)
  // Where:
  // - s_s is the sensor noise

  // x_ML = l.t + l_0                                             (25)
  // Know:
  // - l : sensor ray = (sample - sensor) / ||sample - sensor||
  // - l_0 : sensor position

  // t =  a_x b_x + a_y b_y + a_z b_z /                           (28)
  //      a_x l_x + a_y l_y + a_z l-z
  //
  // a = P[-1] l
  // b = (l_0 - u)

  const covvec3 sensor_to_sample = sample - sensor;
  const covvec3 sensor_ray = covnormalize(sensor_to_sample);  // Verified
  const covvec3 sensor_to_mean = sensor - voxel_mean;

  // Packed data solutions:
  const covvec3 a = solveTriangular(cov_voxel, sensor_ray);
  const covvec3 b_norm = solveTriangular(cov_voxel, sensor_to_mean);

  // const covvec3 a = covariance_inv * sensor_ray;  // Verified (unpacked version)
  // (28)
  // const covreal t = covdot(a, sensor_to_mean) / covdot(a, sensor_ray); // Verified (unpacked version)
  const covreal t = -covdot(a, b_norm) / covdot(a, a);  // Verified

  // (25)
  // Note: maximum_likelyhood is abbreviated to ml in assoicated variable names.
  const covvec3 voxel_maximum_likelyhood = sensor_ray * t + sensor;  // Verified

  // (22)
  // Unverified: json line 264
  // const covreal p_x_ml_given_voxel = std::exp(
  //   -0.5 * covdot(voxel_maximum_likelyhood - voxel_mean, covariance_inv * (voxel_maximum_likelyhood -
  //   voxel_mean)));
  // Corrected:
  const covreal p_x_ml_given_voxel =
    exp(-0.5 * covlength2(solveTriangular(cov_voxel, voxel_maximum_likelyhood - voxel_mean)));

  // (23)
  // Verified: json: line 263
  const covreal sensor_noise_variance = sensor_noise * sensor_noise;
  const covreal p_x_ml_given_sample = exp(-0.5 * covlength2(voxel_maximum_likelyhood - sample) / sensor_noise_variance);

  // Set the scaling factor by converting the miss value to a probability.
  const covreal scaling_factor = 1.0f - (1.0 / (1.0 + exp(miss_value)));
  // Verified: json line 267
  const covreal probability_update = 0.5 - scaling_factor * p_x_ml_given_voxel * (1.0 - p_x_ml_given_sample);

  // Check for NaN
  // This should no longer be occurring.
  if (probability_update == probability_update)
  {
    // Convert the probability to a log value.
    *voxel_value += (float)log(probability_update / ((covreal)1 - probability_update));
  }

  return voxel_maximum_likelyhood;
}

#if !GPUTIL_DEVICE
/// Perform an eigen decomposition on the covariance dat ain @p cov.
/// Requires compilation with the Eigen maths library or it always returns false.
bool ohm_API eigenDecomposition(const CovarianceVoxel *cov, glm::dvec3 *eigenvalues, glm::dmat3 *eigenvectors);


/// Unpack @c cov.trianglar_covariance into a 3x3 covariance matrix.
inline glm::dmat3 covarianceMatrix(const CovarianceVoxel *cov)
{
  glm::dmat3 cov_mat;

  glm::dvec3 *col = &cov_mat[0];
  (*col)[0] = cov->trianglar_covariance[0];
  (*col)[1] = cov->trianglar_covariance[1];
  (*col)[2] = cov->trianglar_covariance[3];

  col = &cov_mat[1];
  (*col)[0] = 0;
  (*col)[1] = cov->trianglar_covariance[2];
  (*col)[2] = cov->trianglar_covariance[4];

  col = &cov_mat[2];
  (*col)[0] = 0;
  (*col)[1] = 0;
  (*col)[2] = cov->trianglar_covariance[5];

  return cov_mat;
}
}  // namespace ohm
#endif  // !GPUTIL_DEVICE

#endif  // COVARIANCEVOXEL_H

/**
 * @author Parikshit Ram
 * @file gmm_train_main.cpp
 *
 * This program trains a mixture of Gaussians on a given data matrix.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */
#include <mlpack/prereqs.hpp>
#include <mlpack/core/util/cli.hpp>
#include <mlpack/core/util/mlpack_main.hpp>

#include "gmm.hpp"
#include "no_constraint.hpp"
#include "diagonal_constraint.hpp"

#include <mlpack/methods/kmeans/refined_start.hpp>

using namespace mlpack;
using namespace mlpack::gmm;
using namespace mlpack::util;
using namespace mlpack::kmeans;
using namespace std;

PROGRAM_INFO("Gaussian Mixture Model (GMM) Training",
    "This program takes a parametric estimate of a Gaussian mixture model (GMM)"
    " using the EM algorithm to find the maximum likelihood estimate.  The "
    "model may be saved and reused by other mlpack GMM tools."
    "\n\n"
    "The input data to train on must be specified with the " +
    PRINT_PARAM_STRING("input") + " parameter, and the number of Gaussians in "
    "the model must be specified with the " + PRINT_PARAM_STRING("gaussians") +
    " parameter.  Optionally, many trials with different random "
    "initializations may be run, and the result with highest log-likelihood on "
    "the training data will be taken.  The number of trials to run is specified"
    " with the " + PRINT_PARAM_STRING("trials") + " parameter.  By default, "
    "only one trial is run."
    "\n\n"
    "The tolerance for convergence and maximum number of iterations of the EM "
    "algorithm are specified with the " + PRINT_PARAM_STRING("tolerance") +
    " and " + PRINT_PARAM_STRING("max_iterations") + " parameters, "
    "respectively.  The GMM may be initialized for training with another model,"
    " specified with the " + PRINT_PARAM_STRING("input_model") + " parameter."
    " Otherwise, the model is initialized by running k-means on the data.  The "
    "k-means clustering initialization can be controlled with the " +
    PRINT_PARAM_STRING("refined_start") + ", " +
    PRINT_PARAM_STRING("samplings") + ", and " +
    PRINT_PARAM_STRING("percentage") + " parameters.  If " +
    PRINT_PARAM_STRING("refined_start") + " is specified, then the "
    "Bradley-Fayyad refined start initialization will be used.  This can often "
    "lead to better clustering results."
    "\n\n"
    "The 'diagonal_covariance' flag will cause the learned covariances to be "
    "diagonal matrices.  This significantly simplifies the model itself and "
    "causes training to be faster, but restricts the ability to fit more "
    "complex GMMs."
    "\n\n"
    "If GMM training fails with an error indicating that a covariance matrix "
    "could not be inverted, make sure that the " +
    PRINT_PARAM_STRING("no_force_positive") + " parameter is not "
    "specified.  Alternately, adding a small amount of Gaussian noise (using "
    "the " + PRINT_PARAM_STRING("noise") + " parameter) to the entire dataset"
    " may help prevent Gaussians with zero variance in a particular dimension, "
    "which is usually the cause of non-invertible covariance matrices."
    "\n\n"
    "The " + PRINT_PARAM_STRING("no_force_positive") + " parameter, if set, "
    "will avoid the checks after each iteration of the EM algorithm which "
    "ensure that the covariance matrices are positive definite.  Specifying "
    "the flag can cause faster runtime, but may also cause non-positive "
    "definite covariance matrices, which will cause the program to crash."
    "\n\n"
    "As an example, to train a 6-Gaussian GMM on the data in " +
    PRINT_DATASET("data") + " with a maximum of 100 iterations of EM and 3 "
    "trials, saving the trained GMM to " + PRINT_MODEL("gmm") + ", the "
    "following command can be used:"
    "\n\n" +
    PRINT_CALL("gmm_train", "input", "data", "gaussians", 6, "trials", 3,
        "output_model", "gmm") +
    "\n\n"
    "To re-train that GMM on another set of data " + PRINT_DATASET("data2") +
    ", the following command may be used: "
    "\n\n" +
    PRINT_CALL("gmm_train", "input_model", "gmm", "input", "data2",
        "gaussians", 6, "output_model", "new_gmm"));

// Parameters for training.
PARAM_MATRIX_IN_REQ("input", "The training data on which the model will be "
    "fit.", "i");
PARAM_INT_IN_REQ("gaussians", "Number of Gaussians in the GMM.", "g");

PARAM_INT_IN("seed", "Random seed.  If 0, 'std::time(NULL)' is used.", "s", 0);
PARAM_INT_IN("trials", "Number of trials to perform in training GMM.", "t", 1);

// Parameters for EM algorithm.
PARAM_DOUBLE_IN("tolerance", "Tolerance for convergence of EM.", "T", 1e-10);
PARAM_FLAG("no_force_positive", "Do not force the covariance matrices to be "
    "positive definite.", "P");
PARAM_INT_IN("max_iterations", "Maximum number of iterations of EM algorithm "
    "(passing 0 will run until convergence).", "n", 250);
PARAM_FLAG("diagonal_covariance", "Force the covariance of the Gaussians to "
    "be diagonal.  This can accelerate training time significantly.", "d");

// Parameters for dataset modification.
PARAM_DOUBLE_IN("noise", "Variance of zero-mean Gaussian noise to add to data.",
    "N", 0);

// Parameters for k-means initialization.
PARAM_FLAG("refined_start", "During the initialization, use refined initial "
    "positions for k-means clustering (Bradley and Fayyad, 1998).", "r");
PARAM_INT_IN("samplings", "If using --refined_start, specify the number of "
    "samplings used for initial points.", "S", 100);
PARAM_DOUBLE_IN("percentage", "If using --refined_start, specify the percentage"
    " of the dataset used for each sampling (should be between 0.0 and 1.0).",
    "p", 0.02);

// Parameters for model saving/loading.
PARAM_MODEL_IN(GMM, "input_model", "Initial input GMM model to start training "
    "with.", "m");
PARAM_MODEL_OUT(GMM, "output_model", "Output for trained GMM model.", "M");

void mlpackMain()
{
  // Check parameters and load data.
  if (CLI::GetParam<int>("seed") != 0)
    math::RandomSeed((size_t) CLI::GetParam<int>("seed"));
  else
    math::RandomSeed((size_t) std::time(NULL));

  const int gaussians = CLI::GetParam<int>("gaussians");
  if (gaussians <= 0)
  {
    Log::Fatal << "Invalid number of Gaussians (" << gaussians << "); must "
        "be greater than or equal to 1." << std::endl;
  }

  if (CLI::HasParam("diagonal_covariance") &&
      CLI::HasParam("no_force_positive"))
    Log::Warn << "--no_force_positive ignored because --diagonal_covariance is "
        << "specified!" << endl;

  if (!CLI::HasParam("output_model"))
    Log::Warn << "--output_model_file is not specified, so no model will be "
        << "saved!" << endl;

  arma::mat dataPoints = std::move(CLI::GetParam<arma::mat>("input"));

  // Do we need to add noise to the dataset?
  if (CLI::HasParam("noise"))
  {
    Timer::Start("noise_addition");
    const double noise = CLI::GetParam<double>("noise");
    dataPoints += noise * arma::randn(dataPoints.n_rows, dataPoints.n_cols);
    Log::Info << "Added zero-mean Gaussian noise with variance " << noise
        << " to dataset." << std::endl;
    Timer::Stop("noise_addition");
  }

  // Initialize GMM.
  GMM gmm(size_t(gaussians), dataPoints.n_rows);

  if (CLI::HasParam("input_model"))
  {
    gmm = std::move(CLI::GetParam<GMM>("input_model"));

    if (gmm.Dimensionality() != dataPoints.n_rows)
      Log::Fatal << "Given input data (with --input_file) has dimensionality "
          << dataPoints.n_rows << ", but the initial model (given with "
          << "--input_model_file) has dimensionality " << gmm.Dimensionality()
          << "!" << endl;
  }

  // Gather parameters for EMFit object.
  const size_t maxIterations = (size_t) CLI::GetParam<int>("max_iterations");
  const double tolerance = CLI::GetParam<double>("tolerance");
  const bool forcePositive = !CLI::HasParam("no_force_positive");
  const bool diagonalCovariance = CLI::HasParam("diagonal_covariance");

  // This gets a bit weird because we need different types depending on whether
  // --refined_start is specified.
  double likelihood;
  if (CLI::HasParam("refined_start"))
  {
    const int samplings = CLI::GetParam<int>("samplings");
    const double percentage = CLI::GetParam<double>("percentage");

    if (samplings <= 0)
      Log::Fatal << "Number of samplings (" << samplings << ") must be greater"
          << " than 0!" << std::endl;

    if (percentage <= 0.0 || percentage > 1.0)
      Log::Fatal << "Percentage for sampling (" << percentage << ") must be "
          << "greater than 0.0 and less than or equal to 1.0!" << std::endl;

    typedef KMeans<metric::SquaredEuclideanDistance, RefinedStart> KMeansType;

    // These are default parameters.
    KMeansType k(1000, metric::SquaredEuclideanDistance(),
        RefinedStart(samplings, percentage));

    // Depending on the value of forcePositive and diagonalCovariance, we have
    // to use different types.
    if (diagonalCovariance)
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<KMeansType, DiagonalConstraint> em(maxIterations, tolerance, k);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
    else if (forcePositive)
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<KMeansType> em(maxIterations, tolerance, k);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
    else
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<KMeansType, NoConstraint> em(maxIterations, tolerance, k);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
  }
  else
  {
    // Depending on the value of forcePositive and diagonalCovariance, we have
    // to use different types.
    if (diagonalCovariance)
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<kmeans::KMeans<>, DiagonalConstraint> em(maxIterations, tolerance);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
    else if (forcePositive)
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<> em(maxIterations, tolerance);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
    else
    {
      // Compute the parameters of the model using the EM algorithm.
      Timer::Start("em");
      EMFit<KMeans<>, NoConstraint> em(maxIterations, tolerance);
      likelihood = gmm.Train(dataPoints, CLI::GetParam<int>("trials"), false,
          em);
      Timer::Stop("em");
    }
  }

  Log::Info << "Log-likelihood of estimate: " << likelihood << "." << endl;

  if (CLI::HasParam("output_model"))
    CLI::GetParam<GMM>("output_model") = std::move(gmm);
}

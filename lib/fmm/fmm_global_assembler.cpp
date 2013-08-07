// Copyright (C) 2011-2012 by the BEM++ Authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "bempp/common/config_ahmed.hpp"
#include "bempp/common/config_trilinos.hpp"

#include "fmm_global_assembler.hpp"
#include "fmm_transform.hpp"
#include "octree_helper.hpp"
#include "octree.hpp"
#include "octree_node.hpp"
#include "discrete_fmm_boundary_operator.hpp"

#include "../assembly/context.hpp"
//#include "../assembly/evaluation_options.hpp"
#include "../assembly/assembly_options.hpp"
#include "../assembly/index_permutation.hpp"
#include "../assembly/discrete_boundary_operator_composition.hpp"
#include "../assembly/discrete_sparse_boundary_operator.hpp"

#include "../common/armadillo_fwd.hpp"
#include "../common/auto_timer.hpp"
#include "../common/boost_shared_array_fwd.hpp"
#include "../common/shared_ptr.hpp"
#include "../common/boost_make_shared_fwd.hpp"
#include "../common/chunk_statistics.hpp"
#include "../fiber/explicit_instantiation.hpp"
#include "../fiber/local_assembler_for_operators.hpp"
#include "../fiber/serial_blas_region.hpp"
#include "../fiber/scalar_traits.hpp"
#include "../space/space.hpp"
#include "../grid/grid.hpp"

#include <stdexcept>
#include <iostream>

#include <boost/type_traits/is_complex.hpp>

#include <tbb/atomic.h>
#include <tbb/parallel_for.h>
#include <tbb/task_scheduler_init.h>
#include <tbb/concurrent_queue.h>

#include <complex>
#include <boost/math/special_functions/bessel.hpp>
#include <boost/math/special_functions/legendre.hpp>


namespace Bempp
{

template <typename BasisFunctionType, typename ResultType>
std::auto_ptr<DiscreteBoundaryOperator<ResultType> >
FmmGlobalAssembler<BasisFunctionType, ResultType>::assembleDetachedWeakForm(
        const Space<BasisFunctionType>& testSpace,
        const Space<BasisFunctionType>& trialSpace,
        const std::vector<LocalAssembler*>& localAssemblers,
        const std::vector<const DiscreteBndOp*>& sparseTermsToAdd,
        const std::vector<ResultType>& denseTermsMultipliers,
        const std::vector<ResultType>& sparseTermsMultipliers,
        const Context<BasisFunctionType, ResultType>& context,
        bool hermitian,
        const FmmTransform<ResultType>& fmmTransform)
{
	//std::cout << "There are " << localAssemblers.size() << " assembler(s)"<< std::endl;
	const AssemblyOptions& options = context.assemblyOptions();
	const FmmOptions& fmmOptions = options.fmmOptions();

	const bool indexWithGlobalDofs = true;//fmmOptions.globalAssemblyBeforeCompression;

	const size_t testDofCount = indexWithGlobalDofs ?
		testSpace.globalDofCount() : testSpace.flatLocalDofCount();
	const size_t trialDofCount = indexWithGlobalDofs ?
		trialSpace.globalDofCount() : trialSpace.flatLocalDofCount();

	std::cout << "assembleDetachedWeakForm: test " << testDofCount;
	std::cout << " trial " << trialDofCount << " herm " << hermitian << std::endl;
	if (hermitian && testDofCount != trialDofCount)
        throw std::invalid_argument("FmmGlobalAssembler::assembleDetachedWeakForm(): "
                                    "you cannot generate a Hermitian weak form "
                                    "using test and trial spaces with different "
                                    "numbers of DOFs");

	// get the bounding box of the test and trial spaces, in order to set 
	// octree size. Octree should encotestDofCountmpass both spaces
	// N.B. will need a bigger octree for evaulated solution on a surface later
	arma::Col<double> lowerBoundTest, upperBoundTest;
	testSpace.grid()->getBoundingBox(lowerBoundTest, upperBoundTest);

	arma::Col<double> lowerBoundTrial, upperBoundTrial;
	trialSpace.grid()->getBoundingBox(lowerBoundTrial, upperBoundTrial);

	// find the min of each row
	arma::Col<double> lowerBound, upperBound;
	lowerBound = arma::min(arma::join_rows(lowerBoundTest, lowerBoundTrial), 1);
	upperBound = arma::max(arma::join_rows(upperBoundTest, upperBoundTrial), 1);

	std::cout << "lower bound = (" << lowerBound[0] << ", ";
	std::cout << lowerBound[1] << ", " << lowerBound[2] << ')' << std::endl;
	std::cout << "upper bound = (" << upperBound[0] << ", ";
	std::cout << upperBound[1] << ", " << upperBound[2] << ')' << std::endl;

	//shared_ptr<FmmTransform<ResultType> > fmmTransform = 
	//	boost::make_shared<FmmTransformPlaneWave<ResultType> >
	//		(fmmOptions.L, fmmOptions.numQuadPoints);

	// Note that in future the octree will need to store dof's for test
	// and trial spaces individually, if the two differ in order
	unsigned int nLevels = fmmOptions.levels;
	shared_ptr<Octree<ResultType> > octree = 
		boost::make_shared<Octree<ResultType> >(nLevels, fmmTransform,
			arma::conv_to<arma::Col<CoordinateType> >::from(lowerBound),
			arma::conv_to<arma::Col<CoordinateType> >::from(upperBound));

	std::vector<Point3D<CoordinateType> > dofCenters;
	if (indexWithGlobalDofs)
		testSpace.getGlobalDofPositions(dofCenters);
	else
		testSpace.getFlatLocalDofPositions(dofCenters);

	std::vector<unsigned int> p2o;
	p2o = octree->assignPoints(hermitian, dofCenters);

	std::cout << "Caching near field interactions" << std::endl;

	OctreeNearHelper<BasisFunctionType, ResultType> octreeNearHelper(
		octree, testSpace, trialSpace, localAssemblers, denseTermsMultipliers, 
		options, p2o, indexWithGlobalDofs);

	unsigned int nLeaves = getNodesPerLevel(octree->levels());
	tbb::parallel_for<unsigned int>(0, nLeaves, octreeNearHelper);
	//octreeHelper.evaluateNearField(octree);

	//std::cout << "Caching trial far-field interactions" << std::endl;
	//octreeHelper.evaluateTrialFarField(octree, fmmTransform);

	//std::cout << "Caching test far-field interactions" << std::endl;
	//octreeHelper.evaluateTestFarField(octree, fmmTransform);

	std::cout << "Caching test and trial far-field interactions" << std::endl;

	OctreeFarHelper<BasisFunctionType, ResultType> octreeFarHelper(
		octree, testSpace, trialSpace, options, p2o, indexWithGlobalDofs, 
		fmmTransform);

	tbb::parallel_for(tbb::blocked_range<unsigned int>(0, nLeaves, 100), octreeFarHelper);

	unsigned int symmetry = NO_SYMMETRY;
	if (hermitian) {
		symmetry |= HERMITIAN;
		if (boost::is_complex<ResultType>())
			symmetry |= SYMMETRIC;
	}

	// this is a problem, need to hide BasisFunctionType argument somehow
	typedef DiscreteFmmBoundaryOperator<ResultType> DiscreteFmmLinOp;
	std::auto_ptr<DiscreteFmmLinOp> fmmOp(
                new DiscreteFmmLinOp(testDofCount, trialDofCount,
                                     octree,
                                     Symmetry(symmetry) ));

	std::auto_ptr<DiscreteBndOp> result;
	result = fmmOp;

	return result;
}

template <typename BasisFunctionType, typename ResultType>
std::auto_ptr<DiscreteBoundaryOperator<ResultType> >
FmmGlobalAssembler<BasisFunctionType, ResultType>::assembleDetachedWeakForm(
        const Space<BasisFunctionType>& testSpace,
        const Space<BasisFunctionType>& trialSpace,
        LocalAssembler& localAssembler,
        const Context<BasisFunctionType, ResultType>& context,
        bool hermitian,
        const FmmTransform<ResultType>& fmmTransform)
{
    std::vector<LocalAssembler*> localAssemblers(1, &localAssembler);
    std::vector<const DiscreteBndOp*> sparseTermsToAdd;
    std::vector<ResultType> denseTermsMultipliers(1, 1.0);
    std::vector<ResultType> sparseTermsMultipliers;

    return assembleDetachedWeakForm(testSpace, trialSpace, localAssemblers,
                            sparseTermsToAdd,
                            denseTermsMultipliers,
                            sparseTermsMultipliers,
                            context, hermitian, fmmTransform);
}

FIBER_INSTANTIATE_CLASS_TEMPLATED_ON_BASIS_AND_RESULT(FmmGlobalAssembler);

} // namespace Bempp

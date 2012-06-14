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

#ifndef bempp_laplace_3d_operator_base_imp_hpp
#define bempp_laplace_3d_operator_base_imp_hpp

#include "laplace_3d_operator_base.hpp"

namespace Bempp
{

template <typename Impl, typename BasisFunctionType, typename ResultType>
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
Laplace3dOperatorBase(
        const Space<BasisFunctionType>& domain,
        const Space<BasisFunctionType>& range,
        const Space<BasisFunctionType>& dualToRange,
        const std::string& label) :
    Base(domain, range, dualToRange, label), m_impl(new Impl)
{
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
Laplace3dOperatorBase(
        const Laplace3dOperatorBase& other) :
    Base(other), m_impl(new Impl(*other.m_impl))
{
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
~Laplace3dOperatorBase()
{
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
const typename Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
CollectionOfKernels&
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
kernels() const
{
    return m_impl->kernels;
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
const typename Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
CollectionOfBasisTransformations&
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
testTransformations() const
{
    return m_impl->transformations;
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
const typename Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
CollectionOfBasisTransformations&
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
trialTransformations() const
{
    return m_impl->transformations;
}

template <typename Impl, typename BasisFunctionType, typename ResultType>
const typename Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
TestKernelTrialIntegral&
Laplace3dOperatorBase<Impl, BasisFunctionType, ResultType>::
integral() const
{
    return m_impl->integral;
}

} // namespace Bempp

#endif

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

#ifndef bempp_surface_normal_dependent_functor_hpp
#define bempp_surface_normal_dependent_functor_hpp

#include "fiber/scalar_traits.hpp"

namespace Bempp
{

template<typename ValueType_>
class SurfaceNormalDependentFunctor{
public:

    typedef ValueType_ ValueType;
    typedef typename ScalarTraits<ValueType_>::RealType CoordinateType;

    virtual int argumentDimension() const=0;

    virtual int resultDimension() const=0;

    virtual void evaluate(const arma::Col<CoordinateType>& point,
                          const arma::Col<CoordinateType>& normal,
                          arma::Col<ValueType>& result) const=0;

};

}

#endif // bempp_surface_normal_dependent_functor_hpp

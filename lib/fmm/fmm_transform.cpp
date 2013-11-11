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

#include "fmm_transform.hpp"
#include "interpolate_on_sphere.hpp"
#include "lebedev.hpp"
#include "../fiber/explicit_instantiation.hpp"

#include <complex>
#include <iostream>		// std::cout
#include <boost/math/special_functions/bessel.hpp>
#include <boost/math/special_functions/legendre.hpp>

#define USE_AMOS_SPECIAL_FUNCTIONS

#if defined USE_AMOS_SPECIAL_FUNCTIONS
#include "amos.hpp"
#endif

namespace Bempp
{

//template <typename ValueType> // must be a real type
//ValueType diff_legendre_p(int n, ValueType x);

//template <typename ValueType>  // must be a real type
//void legendre_roots(unsigned int N, ValueType *roots, ValueType *weights);

template <typename ValueType>
void 
FmmTransform<ValueType>::interpolate(
		unsigned int levelOld,
		unsigned int levelNew,
		const arma::Col<ValueType>& coefficientsOld, 
		arma::Col<ValueType>& coefficientsNew) const
{
	coefficientsNew = coefficientsOld;
}


template <typename ValueType>
FmmHighFreq<ValueType>::FmmHighFreq(ValueType kappa, unsigned int L, unsigned int levels)
	 :	m_kappa(kappa), m_Ls(levels-1),// m_L(L),
		m_interpolatorsUpwards(levels-2), m_interpolatorsDownwards(levels-2), 
		FmmTransform<ValueType>((L+1)*(2*L+1), levels, false)
{
	// calculate minimum L for the leaf (ignoring mesh size for now since just used to
	// scale L). Correct box size should be used later
	unsigned int m_topLevel = 2;
	int prec = 8;//6; // number of digits of precision
	CoordinateType boxsize = 2;
/*	CoordinateType Lmin = abs(kappa)/pow(2, levels)
	 + 1.8*pow(prec, 2./3)*pow(abs(kappa)/pow(2, levels), 1./3);*/
	CoordinateType Lmin = sqrt(3.)*abs(kappa)*boxsize/pow(2, levels)
		+ prec*log10(sqrt(3.)*abs(kappa)*boxsize/pow(2, levels)+M_PI);

	// set number of terms in the expansion used for the M2L operation in each
	// level. N.B. the stored multipole coefficients should be for the leaves alone?
	m_Ls[levels-m_topLevel] = L;
	for (unsigned int level = m_topLevel; level<=levels-1; level++) {
/*		CoordinateType Llevel = abs(kappa)/pow(2, level)
		 + 1.8*pow(prec, 2./3)*pow(abs(kappa)/pow(2, level), 1./3);*/
		CoordinateType Llevel = sqrt(3.)*abs(kappa)*boxsize/pow(2, level)
			+ prec*log10(sqrt(3.)*abs(kappa)*boxsize/pow(2, level)+M_PI);
		m_Ls[level-m_topLevel] = ceil(Llevel);
		//m_Ls[level-m_topLevel] = ceil(L*Llevel/Lmin);
	//	m_Ls[level-m_topLevel] = std::min(21u, m_Ls[level-m_topLevel]);
	}


	for (unsigned int level = m_topLevel; level<=levels; level++) {
		std::cout << "level = " << level << ", ";
		std::cout << "L = " << m_Ls[level-m_topLevel] << ", ngp = " << (m_Ls[level-m_topLevel]+1)*(2*m_Ls[level-m_topLevel]+1) << std::endl;
	}
	for (unsigned int level = m_topLevel; level<=levels-1; level++) {
		m_interpolatorsUpwards[level-m_topLevel] = InterpolateOnSphere<ValueType>(
			m_Ls[level-m_topLevel+1], m_Ls[level-m_topLevel]);
		m_interpolatorsDownwards[level-m_topLevel] = InterpolateOnSphere<ValueType>(
			m_Ls[level-m_topLevel], m_Ls[level-m_topLevel+1]);
	}
	generateGaussPoints();
}

template <typename ValueType>
void FmmHighFreq<ValueType>::generateGaussPoints()
{
	CoordinateType pi = boost::math::constants::pi<CoordinateType>();

/*	std::vector<lebedev_point_t> lebedev = getLebedevSphere(this->quadraturePointCount());

	for (unsigned int p=0; p < this->quadraturePointCount(); p++) {
		this->m_s[3*p+0] = lebedev[p].x;
		this->m_s[3*p+1] = lebedev[p].y;
		this->m_s[3*p+2] = lebedev[p].z;
		this->m_w[p] = lebedev[p].w;
	}
	return;
*/
	// form Gauss-Legendre quadrature points along x = cos(theta)
	unsigned L = m_Ls[m_Ls.size()-1]; // for leaves
	CoordinateType costheta[L+1], sintheta[L+1], wtheta[L+1];
	legendre_roots(L+1, costheta, wtheta);
	for (unsigned int l=0; l<L+1; l++) {
		sintheta[l] = sqrt(1-costheta[l]*costheta[l]);
	}

	// form regular grid along phi (periodic function)
	CoordinateType cosphi[2*L+1], sinphi[2*L+1];
	CoordinateType wphi = 2*pi/(2*L+1);
	for (unsigned int l=0; l<=2*L; l++) {
		cosphi[l] = cos(2*pi*l/(2*L+1));
		sinphi[l] = sin(2*pi*l/(2*L+1));
	}
	// form pairs of Gauss points along (theta, phi)
	unsigned int ind = 0;
	for (unsigned int l=0; l<L+1; l++) {
		for (unsigned int m=0; m<=2*L; m++) {
			this->m_quadraturePoints(0, ind) = sintheta[l]*cosphi[m];	// x
			this->m_quadraturePoints(1, ind) = sintheta[l]*sinphi[m];	// y
			this->m_quadraturePoints(2, ind) = costheta[l];			// z
			this->m_quadratureWeights(ind)   = wtheta[l]*wphi;
			ind++;
		}
	}
}

// Because M2M and L2L are cached, cannot take coefficients are arguments
// therefore need a separate interpolate operation.
// Might be necessary for bbFMM if use a more intelligent interpolation scheme
// e.g. DFT bases
template <typename ValueType>
arma::Mat<ValueType> FmmHighFreq<ValueType>::M2M(
		const arma::Col<CoordinateType>& childPosition, 
		const arma::Col<CoordinateType>& parentPosition,
		unsigned int level) const
{
	// form quadrature point helper arrays
	unsigned L = m_Ls[level-2];
	CoordinateType costheta[L+1], sintheta[L+1], wtheta[L+1];
	legendre_roots(L+1, costheta, wtheta);
	for (unsigned int l=0; l<L+1; l++) {
		sintheta[l] = sqrt(1-costheta[l]*costheta[l]);
	}
	CoordinateType cosphi[2*L+1], sinphi[2*L+1];
	for (unsigned int l=0; l<=2*L; l++) {
		cosphi[l] = cos(2*M_PI*l/(2*L+1));
		sinphi[l] = sin(2*M_PI*l/(2*L+1));
	}
	unsigned int quadraturePointCount = (L+1)*(2*L+1);

	arma::Col<ValueType> T(quadraturePointCount);

	arma::Col<CoordinateType> R = parentPosition - childPosition;
	arma::Col<CoordinateType> khat(3);
	//for (unsigned int p=0; p < this->quadraturePointCount(); p++) {
		//khat = this->getQuadraturePoint(p);
	unsigned int p = 0;
	for (unsigned int ntheta=0; ntheta<L+1; ntheta++) {
		for (unsigned int m=0; m<=2*L; m++) {
			khat(0) = sintheta[ntheta]*cosphi[m];
			khat(1) = sintheta[ntheta]*sinphi[m];
			khat(2) = costheta[ntheta];

			CoordinateType rcostheta = dot(R, khat);
			T[p] = exp(-m_kappa*rcostheta);
			p++;
		}
	} // for each quadrature point
	return T;
}

template <typename ValueType>
arma::Mat<ValueType> FmmHighFreq<ValueType>::L2L(
		const arma::Col<CoordinateType>& parentPosition, 
		const arma::Col<CoordinateType>& childPosition,
		unsigned int level) const
{
	return M2M(parentPosition, childPosition, level);
}


template <typename ValueType>
void 
FmmHighFreq<ValueType>::interpolate(
		unsigned int levelOld,
		unsigned int levelNew,
		const arma::Col<ValueType>& coefficientsOld, 
		arma::Col<ValueType>& coefficientsNew) const
{
	// verify that levelOld and levelNew differ by one only
	if (levelOld > levelNew) { // upwards - interpolation
		m_interpolatorsUpwards[levelNew-2].interpolate(
			coefficientsOld, coefficientsNew);
	} else { // downwards - anterpolation
		m_interpolatorsDownwards[levelOld-2].interpolate(
			coefficientsOld, coefficientsNew);
	}
}

template <typename ValueType>
ValueType getI()
{
	throw std::invalid_argument("getI(): "
		"can only be called for complex result types");
}
template <>
std::complex<float> getI()
{
	return std::complex<float>(0.0, 1.0);
}
template <>
std::complex<double> getI()
{
	return std::complex<double>(0.0, 1.0);
}
double real(double x)
{
	return x;
}
double imag(double x)
{
	return 0.0;
}

template <typename ValueType>
arma::Mat<ValueType> 
FmmHighFreq<ValueType>::M2L(
		const arma::Col<CoordinateType>& sourceCentre, 
		const arma::Col<CoordinateType>& fieldCentre,
		const arma::Col<CoordinateType>& boxSize, // get rid of later
		unsigned int level) const
{
	using namespace boost::math;
	CoordinateType pi = boost::math::constants::pi<CoordinateType>();

	// form quadrature point helper arrays
	unsigned L = m_Ls[level-2];
	CoordinateType costheta[L+1], sintheta[L+1], wtheta[L+1];
	legendre_roots(L+1, costheta, wtheta);
	for (unsigned int l=0; l<L+1; l++) {
		sintheta[l] = sqrt(1-costheta[l]*costheta[l]);
	}
	CoordinateType cosphi[2*L+1], sinphi[2*L+1];
	for (unsigned int l=0; l<=2*L; l++) {
		cosphi[l] = cos(2*pi*l/(2*L+1));
		sinphi[l] = sin(2*pi*l/(2*L+1));
	}
	unsigned int quadraturePointCount = (L+1)*(2*L+1);

	arma::Col<ValueType> T(quadraturePointCount);
	T.fill(0.0);

	arma::Col<CoordinateType> xvec = fieldCentre - sourceCentre;
	CoordinateType r = norm(xvec, 2);
	const arma::Col<CoordinateType>& Rhat = xvec/r;

	ValueType i = getI<ValueType>();

	unsigned int Ldash = L;
	//L = (3*L)/4;
	for (unsigned int l=0; l<=Ldash; l++) {
		ValueType scaledhl;
		CoordinateType CLLdash = 1.;

		if (l <= L) {
		ValueType hl;
#if defined USE_AMOS_SPECIAL_FUNCTIONS
		ValueType z = i*m_kappa*r;
		double zr = real(z);
		double zi = imag(z);
		double nu = l+0.5;
		int kode = 1;
		int N = 1;
		int kind = 1;

		double cyr,cyi; 	// Output values
		int nz,ierr;

		amos::zbesh(&zr,&zi,&nu,&kode,&kind,&N,&cyr,&cyi,&nz,&ierr);
		hl = sqrt(pi/(CoordinateType(2)*z))
			*(CoordinateType(cyr)+i*CoordinateType(cyi));

		std::string amosErrorMessages[6] = {
			"IERR=0, NORMAL RETURN - COMPUTATION COMPLETED",
			"IERR=1, INPUT ERROR   - NO COMPUTATION",
			"IERR=2, OVERFLOW      - NO COMPUTATION, FNU IS"
			"        TOO LARGE OR CABS(Z) IS TOO SMALL OR BOTH",
			"IERR=3, CABS(Z) OR FNU+N-1 LARempp/bempp/lib/fmm/fmm_black_box.cpp:156:47: error:GE - COMPUTATION DONE"
			"        BUT LOSSES OF SIGNIFCANCE BY ARGUMENT"
			"        REDUCTION PRODUCE LESS THAN HALF OF MACHINE"
			"        ACCURACY",
			"IERR=4, CABS(Z) OR FNU+N-1 TOO LARGE - NO COMPUTA-"
			"        TION BECAUSE OF COMPLETE LOSSES OF SIGNIFI-"
			"        CANCE BY ARGUMENT REDUCTION",
			"IERR=5, ERROR              - NO COMPUTATION,"
			"        ALGORITHM TERMINATION CONDITION NOT MET"
		};
		if (ierr != 0) {
			throw std::invalid_argument(std::string("FmmHighFreq::M2L(x1, x2): "
				"AMOS: ")+amosErrorMessages[ierr] );
		}
#else // use boost special functions, only works for purely real or imag kappa
		if (real(m_kappa)==0) { // purely imaginary
			CoordinateType z = -imag(m_kappa)*r;
			hl = sph_bessel(l, z) + i*sph_neumann(l, z);
		} else if (imag(m_kappa)==0 && real(m_kappa)>0) { // purely real and decaying
			CoordinateType zi = real(m_kappa)*r;
			hl = -sqrt(ValueType(2)/(zi*pi))*pow(i,-l)
				*cyl_bessel_k(CoordinateType(l+0.5), zi);
		} else {
			throw std::invalid_argument("FmmHighFreq::M2L(x1, x2): "
     			"boost special functions only support purely real or imaginary args");
		}
#endif
		scaledhl = -m_kappa/(16*pi*pi)*pow(i,l)*CoordinateType(2*l+1)*hl;
		} else { // l > L
			CLLdash = pow(cos((l-L)*pi/(2*(Ldash-L))), 2);
		}

		arma::Col<CoordinateType> khat(3); // slow! do not perform in loop!
//		for (unsigned int p = 0; p < this->quadraturePointCount(); p++) {
//			khat = this->getQuadraturePoint(p);
		// TODO: calculate Pl recursively for the sake of efficiency
		unsigned int p = 0;
		for (unsigned int ntheta=0; ntheta<L+1; ntheta++) {
			for (unsigned int m=0; m<=2*L; m++) { // if symmetric quad point along phi 2L->L+1
				khat(0) = sintheta[ntheta]*cosphi[m];
				khat(1) = sintheta[ntheta]*sinphi[m];
				khat(2) = costheta[ntheta];

				CoordinateType costheta = dot(Rhat, khat);
				if(costheta> 1.0) costheta =  1.0;
				if(costheta<-1.0) costheta = -1.0;
				ValueType Tlocal = scaledhl*CLLdash*legendre_p(l, costheta);
				T(p) += Tlocal;
				//T(p+L+1) += pow(-1, l)*Tlocal; // if symmetric quad point along phi 
				p++;
			}
		}
	}
	return T;
}




FIBER_INSTANTIATE_CLASS_TEMPLATED_ON_RESULT(FmmTransform);
FIBER_INSTANTIATE_CLASS_TEMPLATED_ON_RESULT(FmmHighFreq);

} // namespace Bempp
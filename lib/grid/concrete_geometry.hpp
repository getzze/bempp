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

#ifndef bempp_concrete_geometry_hpp
#define bempp_concrete_geometry_hpp

#include "../common/common.hpp"
#include "../common/eigen_support.hpp"

#include "dune.hpp"
#include "geometry.hpp"

#include "../common/not_implemented_error.hpp"
#include "../fiber/geometrical_data.hpp"
#include <dune/common/fvector.hh>
#include <dune/common/fmatrix.hh>
#include <dune/common/static_assert.hh>
#include <dune/grid/common/grid.hh>
#include <dune/alugrid/2d/alu2dinclude.hh>

#include <memory>

namespace Bempp {

/** \cond FORWARD_DECL */
template <int dim> class ConcreteGeometryFactory;
/** \endcond */

/** \ingroup grid_internal
 *  \brief Wrapper of a Dune geometry of type \p DuneGeometry */
template <int dim_> class ConcreteGeometry : public Geometry {

private:
  std::unique_ptr<DuneGeometry<dim_>> m_dune_geometry;

  void setDuneGeometry(const DuneGeometry<dim_> &dune_geometry) {
    m_dune_geometry.reset(new DuneGeometry<dim_>(dune_geometry));
  }

  template <typename T> void setDuneGeometry(const T &dune_geometry) {

    if (dim_ != T::mydimension)
      throw std::runtime_error("ConcreteGeometry::ConcreteGeometry(): "
                               "Wrong geometry dimension.");

    std::vector<Dune::FieldVector<double, DuneGeometry<dim_>::coorddimension>>
        corners;

    for (int i = 0; i < dune_geometry.corners(); ++i)
      corners.push_back(dune_geometry.corner(i));

    m_dune_geometry.reset(new DuneGeometry<dim_>(dune_geometry.type(), corners));
  }

  ConcreteGeometry() {}

  template <int mydim, typename DuneEntity> friend class ConcreteEntity;
  friend class ConcreteGeometryFactory<dim_>;

public:
  /** \brief Constructor from a DuneGeometry object. */
  explicit ConcreteGeometry(const DuneGeometry<dim_> &dune_geometry)
      : m_dune_geometry(new DuneGeometry<dim_>(dune_geometry)) {}

  /** \brief Constructor from an arbitrary Geometry generated by a grid */
  template<typename T>
  ConcreteGeometry(const T& dune_geometry) {

      setDuneGeometry(dune_geometry);
  }
      
  /** \brief Read-only access to the underlying Dune geometry object. */
  const DuneGeometry<dim_> &duneGeometry() const { return m_dune_geometry; }

  /** \brief Return true if the Dune geometry object has already been set,
   *  false otherwise. */
  bool isInitialized() const { return m_dune_geometry.get(); }

  /** \brief Uninitialize the Dune geometry object. */
  void uninitialize() { m_dune_geometry.reset(); }

  virtual int dim() const { return DuneGeometry<dim_>::mydimension; }

  virtual int dimWorld() const { return DuneGeometry<dim_>::coorddimension; }

  virtual void setupImpl(const Matrix<double> &corners,
                         const Vector<char> &auxData) {
    const int dimWorld = DuneGeometry<dim_>::coorddimension;
    const int cornerCount = corners.cols();
    assert((int)corners.rows() == dimWorld);

    GeometryType type;
    if (DuneGeometry<dim_>::mydimension == 0) {
      assert(cornerCount == 1);
      type.makeVertex();
    } else if (DuneGeometry<dim_>::mydimension == 1) {
      assert(cornerCount == 2);
      type.makeLine();
    } else if (DuneGeometry<dim_>::mydimension == 2) {
      assert(cornerCount == 3 || cornerCount == 4);
      if (cornerCount == 3)
        type.makeTriangle();
      else
        type.makeQuadrilateral();
    } else
      throw NotImplementedError("ConcreteGeometry::setup(): "
                                "not implemented yet for 3D entities");

    std::vector<Dune::FieldVector<double, dimWorld>> duneCorners(cornerCount);
    for (size_t i = 0; i < corners.cols(); ++i)
      for (int j = 0; j < dimWorld; ++j)
        duneCorners[i][j] = corners(j, i);

    m_dune_geometry.reset(
        new DuneGeometry<dim_>(type,duneCorners));
  }

  virtual GeometryType type() const { return m_dune_geometry->type(); }

  virtual bool affine() const { return m_dune_geometry->affine(); }

  virtual int cornerCount() const { return m_dune_geometry->corners(); }

  virtual void getCornersImpl(Matrix<double> &c) const {
    const int cdim = DuneGeometry<dim_>::coorddimension;
    const int n = m_dune_geometry->corners();
    c.resize(cdim, n);

    /* TODO: In future this copying should be optimised away by casting
    appropriate columns of c to Dune field vectors. But this
    can't be done until unit tests are in place. */
    typename DuneGeometry<dim_>::GlobalCoordinate g;
    for (int j = 0; j < n; ++j) {
      g = m_dune_geometry->corner(j);
      for (int i = 0; i < cdim; ++i)
        c(i, j) = g[i];
    }
  }

  virtual void local2globalImpl(const Matrix<double> &local,
                                Matrix<double> &global) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::local2global(): invalid "
                                  "dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    global.resize(cdim, n);

    /* TODO: Optimise (get rid of data copying). */
    typename DuneGeometry<dim_>::GlobalCoordinate g;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t j = 0; j < n; ++j) {
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, j);
      g = m_dune_geometry->global(l);
      for (int i = 0; i < cdim; ++i)
        global(i, j) = g[i];
    }
  }

  virtual void global2localImpl(const Matrix<double> &global,
                                Matrix<double> &local) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)global.rows() != cdim)
      throw std::invalid_argument("Geometry::global2local(): invalid "
                                  "dimensions of the 'global' array");
#endif
    const size_t n = global.cols();
    local.resize(mdim, n);

    /* TODO: Optimise (get rid of data copying). */
    typename DuneGeometry<dim_>::GlobalCoordinate g;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t j = 0; j < n; ++j) {
      for (int i = 0; i < cdim; ++i)
        g[i] = global(i, j);
      l = m_dune_geometry->local(g);
      for (int i = 0; i < mdim; ++i)
        local(i, j) = l[i];
    }
  }

  virtual void
  getIntegrationElementsImpl(const Matrix<double> &local,
                             RowVector<double> &int_element) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::local2global(): invalid "
                                  "dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    int_element.resize(n);

    /* TODO: Optimise (get rid of data copying). */
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t j = 0; j < n; ++j) {
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, j);
      double ie = m_dune_geometry->integrationElement(l);
      int_element(j) = ie;
    }
  }

  virtual double volume() const { return m_dune_geometry->volume(); }

  virtual void getCenterImpl(Eigen::Ref<Vector<double>> c) const {
    const int cdim = DuneGeometry<dim_>::coorddimension;
    c.resize(cdim);

    /* TODO: Optimise (get rid of data copying). */
    typename DuneGeometry<dim_>::GlobalCoordinate g = m_dune_geometry->center();
    for (int i = 0; i < cdim; ++i)
      c(i) = g[i];
  }

  virtual void
  getJacobiansTransposedImpl(const Matrix<double> &local,
                             std::vector<Matrix<double>>& jacobian_t) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::getJacobiansTransposed(): "
                                  "invalid dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    jacobian_t.resize(n);
    //jacobian_t.resize(mdim, cdim, n);

    /* Unfortunately Dune::FieldMatrix (the underlying type of
    JacobianTransposed) stores elements rowwise, while Armadillo does it
    columnwise. Hence element-by-element filling of jacobian_t seems
    unavoidable). */
    // typename DuneGeometry::JacobianTransposed j_t;
    // Dune::FieldMatrix<double,mdim,cdim> j_t;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t k = 0; k < n; ++k) {
      /* However, this bit of data copying could be avoided. */
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, k);
      Dune::FieldMatrix<double, mdim, cdim> j_t =
          m_dune_geometry->jacobianTransposed(l);
      jacobian_t[k].resize(mdim,cdim);
      for (int j = 0; j < cdim; ++j)
        for (int i = 0; i < mdim; ++i)
          jacobian_t[k](i, j) = j_t[i][j];
    }
  }

  virtual void
  getJacobiansTransposedImpl(const Matrix<double> &local,
                             Fiber::_3dArray<double>& jacobian_t) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::getJacobiansTransposed(): "
                                  "invalid dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    jacobian_t.set_size(mdim, cdim, n);

    /* Unfortunately Dune::FieldMatrix (the underlying type of
    JacobianTransposed) stores elements rowwise, while Armadillo does it
    columnwise. Hence element-by-element filling of jacobian_t seems
    unavoidable). */
    // typename DuneGeometry::JacobianTransposed j_t;
    // Dune::FieldMatrix<double,mdim,cdim> j_t;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t k = 0; k < n; ++k) {
      /* However, this bit of data copying could be avoided. */
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, k);
      Dune::FieldMatrix<double, mdim, cdim> j_t =
          m_dune_geometry->jacobianTransposed(l);
      for (int j = 0; j < cdim; ++j)
        for (int i = 0; i < mdim; ++i)
          jacobian_t(i, j, k) = j_t[i][j];
    }
  }


  virtual void getJacobianInversesTransposedImpl(
      const Matrix<double> &local,
      std::vector<Matrix<double>> &jacobian_inv_t) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::getJacobianInversesTransposed(): "
                                  "invalid dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    jacobian_inv_t.resize(n);
    //jacobian_inv_t.resize(cdim, mdim, n);

    // typename DuneGeometry::Jacobian j_inv_t;
    // Dune::FieldMatrix<double,cdim,mdim> j_inv_t;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t k = 0; k < n; ++k) {
      /** \fixme However, this bit of data copying could be avoided. */
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, k);
      Dune::FieldMatrix<double, cdim, mdim> j_inv_t =
          m_dune_geometry->jacobianInverseTransposed(l);
      jacobian_inv_t[k].resize(cdim,mdim);
      for (int j = 0; j < mdim; ++j)
        for (int i = 0; i < cdim; ++i)
          jacobian_inv_t[k](i, j) = j_inv_t[i][j];
    }
  }

  virtual void getJacobianInversesTransposedImpl(
      const Matrix<double> &local,
      Fiber::_3dArray<double>& jacobian_inv_t) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;
#ifndef NDEBUG
    if ((int)local.rows() != mdim)
      throw std::invalid_argument("Geometry::getJacobianInversesTransposed(): "
                                  "invalid dimensions of the 'local' array");
#endif
    const size_t n = local.cols();
    jacobian_inv_t.set_size(cdim, mdim, n);

    // typename DuneGeometry::Jacobian j_inv_t;
    // Dune::FieldMatrix<double,cdim,mdim> j_inv_t;
    typename DuneGeometry<dim_>::LocalCoordinate l;
    for (size_t k = 0; k < n; ++k) {
      /** \fixme However, this bit of data copying could be avoided. */
      for (int i = 0; i < mdim; ++i)
        l[i] = local(i, k);
      Dune::FieldMatrix<double, cdim, mdim> j_inv_t =
          m_dune_geometry->jacobianInverseTransposed(l);
      for (int j = 0; j < mdim; ++j)
        for (int i = 0; i < cdim; ++i)
          jacobian_inv_t(i, j, k) = j_inv_t[i][j];
    }
  }


  virtual void getNormalsImpl(const Matrix<double> &local,
                              Matrix<double> &normal) const {
    Fiber::_3dArray<double> jacobian_t;
    getJacobiansTransposed(local, jacobian_t);
    calculateNormals(jacobian_t, normal);
  }

  virtual void getDataImpl(size_t what, const Matrix<double> &local,
                           Fiber::GeometricalData<double> &data) const {
    // In this first implementation we call the above virtual functions as
    // required.
    // In future some optimisations (elimination of redundant calculations)
    // might be possible.

    typedef ConcreteGeometry<dim_> This; // to avoid virtual function
                                                 // calls

    if (what & Fiber::GLOBALS)
      This::local2global(local, data.globals);
    if (what & Fiber::INTEGRATION_ELEMENTS)
      This::getIntegrationElements(local, data.integrationElements);
    if (what & Fiber::JACOBIANS_TRANSPOSED || what & Fiber::NORMALS)
      This::getJacobiansTransposed(local, data.jacobiansTransposed);
    if (what & Fiber::JACOBIAN_INVERSES_TRANSPOSED)
      This::getJacobianInversesTransposed(local,
                                          data.jacobianInversesTransposed);
    if (what & Fiber::NORMALS)
      calculateNormals(data.jacobiansTransposed, data.normals);
  }

private:
  void calculateNormals(const Fiber::_3dArray<double> &jt,
                        Matrix<double> &normals) const {
    const int mdim = DuneGeometry<dim_>::mydimension;
    const int cdim = DuneGeometry<dim_>::coorddimension;

    if (mdim != cdim - 1)
      throw std::logic_error("ConcreteGeometry::calculateNormals(): "
                             "normal vectors are defined only for "
                             "entities of dimension (worldDimension - 1)");

    const size_t pointCount = jt.extent(2); // jt.n_slices;
    normals.resize(cdim, pointCount);

    // First calculate normal vectors of arbitrary length

    // Compile-time if
    if (cdim == 3)
      for (size_t i = 0; i < pointCount; ++i) {
        normals(0, i) = jt(0, 1, i) * jt(1, 2, i) - jt(0, 2, i) * jt(1, 1, i);
        normals(1, i) = jt(0, 2, i) * jt(1, 0, i) - jt(0, 0, i) * jt(1, 2, i);
        normals(2, i) = jt(0, 0, i) * jt(1, 1, i) - jt(0, 1, i) * jt(1, 0, i);
      }
    else if (cdim == 2)
      for (size_t i = 0; i < pointCount; ++i) {
        normals(0, i) = jt(0, 1, i);
        normals(1, i) = jt(0, 0, i);
      }
    else if (cdim == 1) // probably unnecessary
      for (size_t i = 0; i < pointCount; ++i)
        normals(0, i) = 1.;
    else
      throw std::runtime_error("ConcreteGeometry::calculateNormals(): "
                               "Normal vector is not defined for "
                               "zero-dimensional space");

    // Now set vector length to 1.

    for (size_t i = 0; i < pointCount; ++i) {
      double sum = 0.;
      for (int dim = 0; dim < cdim; ++dim)
        sum += normals(dim, i) * normals(dim, i);
      double invLength = 1. / sqrt(sum);
      for (size_t j = 0; j < cdim; ++j)
        normals(j, i) *= invLength;
    }
  }
};

} // namespace Bempp

#endif

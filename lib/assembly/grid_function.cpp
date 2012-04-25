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

#include "grid_function.hpp"

#include "config_trilinos.hpp"

#include "assembly_options.hpp"
#include "discrete_linear_operator.hpp"
#include "discrete_sparse_linear_operator.hpp"
#include "identity_operator.hpp"

#include "../common/stl_io.hpp"
#include "../fiber/basis.hpp"
#include "../fiber/explicit_instantiation.hpp"
#include "../fiber/function.hpp"
#include "../fiber/local_assembler_factory.hpp"
#include "../fiber/local_assembler_for_grid_functions.hpp"
#include "../fiber/opencl_handler.hpp"
#include "../fiber/raw_grid_geometry.hpp"
#include "../grid/geometry_factory.hpp"
#include "../grid/grid.hpp"
#include "../grid/grid_view.hpp"
#include "../grid/entity_iterator.hpp"
#include "../grid/entity.hpp"
#include "../grid/mapper.hpp"
#include "../space/space.hpp"

#include <set>

#ifdef WITH_TRILINOS
#include <Amesos.h>
#include <Amesos_BaseSolver.h>
#include <Epetra_Map.h>
#include <Epetra_MultiVector.h>
#include <Epetra_CrsMatrix.h>
#include <Epetra_SerialComm.h>
#endif

// TODO: rewrite the constructor of OpenClHandler.
// It should take a bool useOpenCl and *in addition to that* openClOptions.
// The role of the latter should be to e.g. select the device to use
// and other configurable execution parameters.
// If there are no such parameters, OpenClOptions should just be removed.

namespace Bempp
{

// Type-agnostic wrapper for the Amesos solver
namespace
{

#ifdef WITH_TRILINOS
template <typename ValueType>
void solveWithAmesos(Epetra_CrsMatrix& mat, arma::Mat<ValueType>& solution,
                     const arma::Mat<ValueType>& rhs);

template <>
void solveWithAmesos<double>(Epetra_CrsMatrix& mat,
                             arma::Mat<double>& armaSolution,
                             const arma::Mat<double>& armaRhs)
{
    const int rowCount = mat.NumGlobalRows();
    assert(rowCount == mat.NumGlobalCols());
    assert(rowCount == armaSolution.n_rows);
    assert(rowCount == armaRhs.n_rows);
    const int rhsCount = armaRhs.n_cols;
    assert(rhsCount == armaSolution.n_cols);

    Epetra_Map map(rowCount, 0 /* base index */, Epetra_SerialComm());
    Epetra_MultiVector solution(View, map, armaSolution.memptr(),
                                rowCount, rhsCount);
    Epetra_MultiVector rhs(View, map, const_cast<double*>(armaRhs.memptr()),
                           rowCount, rhsCount);
    Epetra_LinearProblem problem(&mat, &solution, &rhs);

    Amesos amesosFactory;
    const char* solverName = "Amesos_Klu";
    if (!amesosFactory.Query(solverName))
        throw std::runtime_error("GridFunction::GridFunction(): "
                                 "Amesos_Klu solver not available");
    std::auto_ptr<Amesos_BaseSolver> solver(
                amesosFactory.Create("Amesos_Klu", problem));
    if (!solver.get())
        throw std::runtime_error("GridFunction::GridFunction(): "
                                 "Amesos solver could not be constructed");

    if (solver->SymbolicFactorization())
        throw std::runtime_error("GridFunction::GridFunction(): "
                                 "Symbolic factorisation with Amesos failed");
    if (solver->NumericFactorization())
        throw std::runtime_error("GridFunction::GridFunction(): "
                                 "Numeric factorisation with Amesos failed");
    if (solver->Solve())
        throw std::runtime_error("GridFunction::GridFunction(): "
                                 "Amesos solve failed");
}

template <>
void solveWithAmesos<float>(Epetra_CrsMatrix& mat,
                            arma::Mat<float>& armaSolution,
                            const arma::Mat<float>& armaRhs)
{
    // Right now we only support single rhs vectors
    assert(armaSolution.n_cols == 1);
    assert(armaRhs.n_cols == 1);

    arma::Col<double> solution_double(armaSolution.n_rows);
    std::copy(armaSolution.begin(), armaSolution.end(), solution_double.begin());
    arma::Col<double> rhs_double(armaRhs.n_rows);
    std::copy(armaRhs.begin(), armaRhs.end(), rhs_double.begin());

    solveWithAmesos<double>(mat, solution_double, rhs_double);

    std::copy(solution_double.begin(), solution_double.end(), armaSolution.begin());
}

template <>
void solveWithAmesos<std::complex<float> >(
        Epetra_CrsMatrix& mat,
        arma::Mat<std::complex<float> >& armaSolution,
        const arma::Mat<std::complex<float> >& armaRhs)
{
    // Right now we only support single rhs vectors
    assert(armaSolution.n_cols == 1);
    assert(armaRhs.n_cols == 1);

    // Solve for the real and imaginary part separately
    // (The copy of the solution (before solving) is probably not necessary...)
    arma::Mat<double> solution_double(armaSolution.n_rows, 2);
    for (int i = 0; i < armaSolution.n_rows; ++i)
    {
        solution_double(i, 0) = armaSolution(i).real();
        solution_double(i, 1) = armaSolution(i).imag();
    }
    arma::Mat<double> rhs_double(armaRhs.n_rows, 2);
    for (int i = 0; i < armaRhs.n_rows; ++i)
    {
        solution_double(i, 0) = armaRhs(i).real();
        solution_double(i, 1) = armaRhs(i).imag();
    }

    solveWithAmesos<double>(mat, solution_double, rhs_double);
    for (int i = 0; i < armaSolution.n_rows; ++i)
        armaSolution(i) = std::complex<float>(solution_double(i, 0),
                                              solution_double(i, 1));
}

template <>
void solveWithAmesos<std::complex<double> >(
        Epetra_CrsMatrix& mat,
        arma::Mat<std::complex<double> >& armaSolution,
        const arma::Mat<std::complex<double> >& armaRhs)
{
    // Right now we only support single rhs vectors
    assert(armaSolution.n_cols == 1);
    assert(armaRhs.n_cols == 1);

    // Solve for the real and imaginary part separately
    // (The copy of the solution (before solving) is probably not necessary...)
    arma::Mat<double> solution_double(armaSolution.n_rows, 2);
    for (int i = 0; i < armaSolution.n_rows; ++i)
    {
        solution_double(i, 0) = armaSolution(i).real();
        solution_double(i, 1) = armaSolution(i).imag();
    }
    arma::Mat<double> rhs_double(armaRhs.n_rows, 2);
    for (int i = 0; i < armaRhs.n_rows; ++i)
    {
        solution_double(i, 0) = armaRhs(i).real();
        solution_double(i, 1) = armaRhs(i).imag();
    }

    solveWithAmesos<double>(mat, solution_double, rhs_double);
    for (int i = 0; i < armaSolution.n_rows; ++i)
        armaSolution(i) = std::complex<double>(solution_double(i, 0),
                                               solution_double(i, 1));
}
#endif

} // namespace

template <typename ArgumentType, typename ResultType>
GridFunction<ArgumentType, ResultType>::GridFunction(
        const Space<ArgumentType>& space,
        const Fiber::Function<ResultType>& function,
        const LocalAssemblerFactory& factory,
        const AssemblyOptions& assemblyOptions) :
    m_space(space)
{
    arma::Col<ResultType> projections =
            calculateProjections(function, space, factory, assemblyOptions);

    AssemblyOptions idAssemblyOptions(assemblyOptions);
    IdentityOperator<ArgumentType, ResultType> id(space, space);
    std::auto_ptr<DiscreteLinearOperator<ResultType> > discreteId =
            id.assembleWeakForm(factory, idAssemblyOptions);

    // Solve the system id * m_coefficients = projections
#ifdef WITH_TRILINOS
    if (assemblyOptions.operatorRepresentation() != assemblyOptions.DENSE) {
        DiscreteSparseLinearOperator<ResultType>& sparseDiscreteId =
                dynamic_cast<DiscreteSparseLinearOperator<ResultType>&>(*discreteId);
        Epetra_CrsMatrix& epetraMat = sparseDiscreteId.epetraMatrix();

        const int coefficientCount = space.globalDofCount();
        m_coefficients.set_size(coefficientCount);
        m_coefficients.fill(0.);

        solveWithAmesos(epetraMat, m_coefficients, projections);
    } else {
        m_coefficients = arma::solve(discreteId->asMatrix(), projections);
    }
#else
    m_coefficients = arma::solve(discreteId->asMatrix(), projections);
#endif
}

template <typename ArgumentType, typename ResultType>
GridFunction<ArgumentType, ResultType>::GridFunction(
        const Space<ArgumentType>& space,
        const arma::Col<ResultType>& coefficients) :
    m_space(space), m_coefficients(coefficients)
{
    if (!m_space.dofsAssigned())
        throw std::runtime_error(
                "GridFunction::GridFunction(): "
                "degrees of freedom of the provided space must be assigned "
                "beforehand");
    if (m_space.globalDofCount() != m_coefficients.n_rows)
        throw std::runtime_error(
                "GridFunction::GridFunction(): "
                "dimension of coefficients does not match the number of global "
                "DOFs in the provided function space");
}

template <typename ArgumentType, typename ResultType>
GridFunction<ArgumentType, ResultType>::GridFunction(
        const Space<ArgumentType>& space,
        const Vector<ResultType>& coefficients) :
    m_space(space), m_coefficients(coefficients.asArmadilloVector())
{
    if (!m_space.dofsAssigned())
        throw std::runtime_error(
                "GridFunction::GridFunction(): "
                "degrees of freedom of the provided space must be assigned "
                "beforehand");
    if (m_space.globalDofCount() != m_coefficients.n_rows)
        throw std::runtime_error(
                "GridFunction::GridFunction(): "
                "dimension of coefficients does not match the number of global "
                "DOFs in the provided function space");
}

template <typename ArgumentType, typename ResultType>
const Grid& GridFunction<ArgumentType, ResultType>::grid() const
{
    return m_space.grid();
}

template <typename ArgumentType, typename ResultType>
const Space<ArgumentType>& GridFunction<ArgumentType, ResultType>::space() const
{
    return m_space;
}

template <typename ArgumentType, typename ResultType>
int GridFunction<ArgumentType, ResultType>::codomainDimension() const
{
    return m_space.codomainDimension();
}

template <typename ArgumentType, typename ResultType>
Vector<ResultType> GridFunction<ArgumentType, ResultType>::coefficients() const
{
    return Vector<ResultType>(m_coefficients);
}

template <typename ArgumentType, typename ResultType>
void GridFunction<ArgumentType, ResultType>::setCoefficients(
        const Vector<ResultType>& coeffs)
{
    if (coeffs.size() != m_space.globalDofCount())
        throw std::invalid_argument(
                "GridFunction::setCoefficients(): dimension of the provided "
                "vector does not match the number of global DOFs");
    m_coefficients = coeffs.asArmadilloVector();
}

// Redundant, in fact -- can be obtained directly from Space
template <typename ArgumentType, typename ResultType>
const Fiber::Basis<ArgumentType>& GridFunction<ArgumentType, ResultType>::basis(
        const Entity<0>& element) const
{
    return m_space.basis(element);
}

template <typename ArgumentType, typename ResultType>
void GridFunction<ArgumentType, ResultType>::getLocalCoefficients(
        const Entity<0>& element, std::vector<ResultType>& coeffs) const
{
    std::vector<GlobalDofIndex> gdofIndices;
    m_space.globalDofs(element, gdofIndices);
    const int gdofCount = gdofIndices.size();
    coeffs.resize(gdofCount);
    for (int i = 0; i < gdofCount; ++i)
        coeffs[i] = m_coefficients(gdofIndices[i]);
}

template <typename ArgumentType, typename ResultType>
void GridFunction<ArgumentType, ResultType>::exportToVtk(
        VtkWriter::DataType dataType,
        const char* dataLabel,
        const char* fileNamesBase, const char* filesPath,
        VtkWriter::OutputType type) const
{
    arma::Mat<ResultType> data;
    evaluateAtSpecialPoints(dataType, data);

    std::auto_ptr<GridView> view = m_space.grid().leafView();
    std::auto_ptr<VtkWriter> vtkWriter = view->vtkWriter();

    throw std::logic_error("NOT WORKING!!!");
//    if (dataType == VtkWriter::CELL_DATA)
//        vtkWriter->addCellData(data, dataLabel);
//    else // VERTEX_DATA
//        vtkWriter->addVertexData(data, dataLabel);
    if (filesPath)
        vtkWriter->pwrite(fileNamesBase, filesPath, ".", type);
    else
        vtkWriter->write(fileNamesBase, type);
}

template <typename ArgumentType, typename ResultType>
arma::Col<ResultType>
GridFunction<ArgumentType, ResultType>::calculateProjections(
        const Fiber::Function<ResultType>& globalFunction,
        const Space<ArgumentType>& space,
        const LocalAssemblerFactory& factory,
        const AssemblyOptions& options) const
{
    if (!space.dofsAssigned())
        throw std::runtime_error(
                "GridFunction::calculateProjections(): "
                "degrees of freedom of the provided space must be assigned "
                "before calling calculateProjections()");

    // Prepare local assembler

    const Grid& grid = space.grid();
    std::auto_ptr<GridView> view = grid.leafView();
    const int elementCount = view->entityCount(0);

    // Gather geometric data
    Fiber::RawGridGeometry<CoordinateType> rawGeometry(grid.dim(), grid.dimWorld());
    view->getRawElementData(
                rawGeometry.vertices(), rawGeometry.elementCornerIndices(),
                rawGeometry.auxData());

    // Make geometry factory
    std::auto_ptr<GeometryFactory> geometryFactory =
            grid.elementGeometryFactory();

    // Get pointers to test and trial bases of each element
    std::vector<const Fiber::Basis<ArgumentType>*> testBases;
    testBases.reserve(elementCount);

    std::auto_ptr<EntityIterator<0> > it = view->entityIterator<0>();
    while (!it->finished()) {
        const Entity<0>& element = it->entity();
        testBases.push_back(&space.basis(element));
        it->next();
    }

    // Get reference to the test expression
    const Fiber::Expression<ArgumentType>& testExpression =
            space.shapeFunctionValueExpression();

    // Now create the assembler
    Fiber::OpenClHandler<CoordinateType, int> openClHandler(options.openClOptions());
    openClHandler.pushGeometry (rawGeometry.vertices(),
                                rawGeometry.elementCornerIndices());

    std::auto_ptr<LocalAssembler> assembler =
            factory.make(*geometryFactory, rawGeometry,
                         testBases,
                         testExpression, globalFunction,
                         openClHandler);

    return reallyCalculateProjections(
                space, *assembler, options);
}

template <typename ArgumentType, typename ResultType>
arma::Col<ResultType>
GridFunction<ArgumentType, ResultType>::reallyCalculateProjections(
        const Space<ArgumentType>& space,
        LocalAssembler& assembler,
        const AssemblyOptions& options) const
{
    // TODO: parallelise using TBB (the parameter options will then start be used)

    // Get the grid's leaf view so that we can iterate over elements
    std::auto_ptr<GridView> view = space.grid().leafView();
    const int elementCount = view->entityCount(0);

    // Global DOF indices corresponding to local DOFs on elements
    std::vector<std::vector<GlobalDofIndex> > testGlobalDofs(elementCount);

    // Gather global DOF lists
    const Mapper& mapper = view->elementMapper();
    std::auto_ptr<EntityIterator<0> > it = view->entityIterator<0>();
    while (!it->finished()) {
        const Entity<0>& element = it->entity();
        const int elementIndex = mapper.entityIndex(element);
        space.globalDofs(element, testGlobalDofs[elementIndex]);
        it->next();
    }

    // Make a vector of all element indices
    std::vector<int> testIndices(elementCount);
    for (int i = 0; i < elementCount; ++i)
        testIndices[i] = i;

    // Create the weak form's column vector
    arma::Col<ResultType> result(space.globalDofCount());
    result.fill(0.);

    std::vector<arma::Col<ResultType> > localResult;
    // Evaluate local weak forms
    assembler.evaluateLocalWeakForms(testIndices, localResult);

    // Loop over test indices
    for (int testIndex = 0; testIndex < elementCount; ++testIndex)
        // Add the integrals to appropriate entries in the global weak form
        for (int testDof = 0; testDof < testGlobalDofs[testIndex].size(); ++testDof)
            result(testGlobalDofs[testIndex][testDof]) +=
                    localResult[testIndex](testDof);

    // Return the vector of projections <phi_i, f>
    return result;
}

template <typename ArgumentType, typename ResultType>
void GridFunction<ArgumentType, ResultType>::evaluateAtSpecialPoints(
        VtkWriter::DataType dataType, arma::Mat<ResultType>& result) const
{
    if (dataType != VtkWriter::CELL_DATA && dataType != VtkWriter::VERTEX_DATA)
        throw std::invalid_argument("GridFunction::evaluateAtSpecialPoints(): "
                                    "invalid data type");

    const Grid& grid = m_space.grid();
    const int gridDim = grid.dim();
    const int elementCodim = 0;
    const int vertexCodim = grid.dim();

    std::auto_ptr<GridView> view = grid.leafView();
    const int elementCount = view->entityCount(elementCodim);
    const int vertexCount = view->entityCount(vertexCodim);

    result.set_size(codomainDimension(),
                    dataType == VtkWriter::CELL_DATA ? elementCount : vertexCount);
    result.fill(0.);

    // Number of elements contributing to each column in result
    // (this will be greater than 1 for VERTEX_DATA)
    std::vector<int> multiplicities(vertexCount);
    std::fill(multiplicities.begin(), multiplicities.end(), 0);

    // Gather geometric data
    Fiber::RawGridGeometry<CoordinateType> rawGeometry(grid.dim(), grid.dimWorld());
    view->getRawElementData(
                rawGeometry.vertices(), rawGeometry.elementCornerIndices(),
                rawGeometry.auxData());

    // Make geometry factory
    std::auto_ptr<GeometryFactory> geometryFactory =
            grid.elementGeometryFactory();
    std::auto_ptr<typename GeometryFactory::Geometry> geometry(
                geometryFactory->make());
    Fiber::GeometricalData<CoordinateType> geomData;

    // For each element, get its basis and corner count (this is sufficient
    // to identify its geometry) as well as its local coefficients
    typedef std::pair<const Fiber::Basis<ArgumentType>*, int> BasisAndCornerCount;
    typedef std::vector<BasisAndCornerCount> BasisAndCornerCountVector;
    BasisAndCornerCountVector basesAndCornerCounts(elementCount);
    std::vector<std::vector<ResultType> > localCoefficients(elementCount);
    {
        std::auto_ptr<EntityIterator<0> > it = view->entityIterator<0>();
        for (int e = 0; e < elementCount; ++e) {
            const Entity<0>& element = it->entity();
            basesAndCornerCounts[e] = BasisAndCornerCount(
                        &m_space.basis(element), rawGeometry.elementCornerCount(e));
            getLocalCoefficients(element, localCoefficients[e]);
            it->next();
        }
    }

    typedef std::set<BasisAndCornerCount> BasisAndCornerCountSet;
    BasisAndCornerCountSet uniqueBasesAndCornerCounts(
                basesAndCornerCounts.begin(), basesAndCornerCounts.end());

    // Find out which basis data need to be calculated
    int basisDeps = 0, geomDeps = 0;
    // Find out which geometrical data need to be calculated, in addition
    // to those needed by the kernel
    const Fiber::Expression<ArgumentType>& expression =
            m_space.shapeFunctionValueExpression();
    expression.addDependencies(basisDeps, geomDeps);

    // Loop over unique combinations of basis and element corner count
    typedef typename BasisAndCornerCountSet::const_iterator
            BasisAndCornerCountSetConstIt;
    for (BasisAndCornerCountSetConstIt it = uniqueBasesAndCornerCounts.begin();
         it != uniqueBasesAndCornerCounts.end(); ++it) {
        const BasisAndCornerCount& activeBasisAndCornerCount = *it;
        const Fiber::Basis<ArgumentType>& activeBasis =
                *activeBasisAndCornerCount.first;
        int activeCornerCount = activeBasisAndCornerCount.second;

        // Set the local coordinates of either all vertices or the barycentre
        // of the active element type
        arma::Mat<CoordinateType> local;
        if (dataType == VtkWriter::CELL_DATA) {
            local.set_size(gridDim, 1);

            // We could actually use Dune for these assignements
            if (gridDim == 1 && activeCornerCount == 2) {
                // linear segment
                local(0, 0) = 0.5;
            } else if (gridDim == 2 && activeCornerCount == 3) {
                // triangle
                local(0, 0) = 1./3.;
                local(1, 0) = 1./3.;
            } else if (gridDim == 2 && activeCornerCount == 4) {
                // quadrilateral
                local(0, 0) = 0.5;
                local(1, 0) = 0.5;
            } else
                throw std::runtime_error("GridFunction::evaluateAtVertices(): "
                                         "unsupported element type");
        } else { // VERTEX_DATA
            local.set_size(gridDim, activeCornerCount);

            // We could actually use Dune for these assignements
            if (gridDim == 1 && activeCornerCount == 2) {
                // linear segment
                local(0, 0) = 0.;
                local(0, 1) = 1.;
            } else if (gridDim == 2 && activeCornerCount == 3) {
                // triangle
                local.fill(0.);
                local(0, 1) = 1.;
                local(1, 2) = 1.;
            } else if (gridDim == 2 && activeCornerCount == 4) {
                // quadrilateral
                local.fill(0.);
                local(0, 1) = 1.;
                local(1, 2) = 1.;
                local(0, 3) = 1.;
                local(1, 3) = 1.;
            } else
                throw std::runtime_error("GridFunction::evaluateAtVertices(): "
                                         "unsupported element type");
        }

        // Get basis data
        Fiber::BasisData<ArgumentType> basisData;
        activeBasis.evaluate(basisDeps, local, ALL_DOFS, basisData);

        Fiber::BasisData<ResultType> functionData;
        if (basisDeps & Fiber::VALUES)
            functionData.values.set_size(basisData.values.n_rows,
                                         1, // just one function
                                         basisData.values.n_slices);
        if (basisDeps & Fiber::DERIVATIVES)
            functionData.derivatives.set_size(basisData.derivatives.extent(0),
                                              basisData.derivatives.extent(1),
                                              1, // just one function
                                              basisData.derivatives.extent(3));
        arma::Cube<ResultType> functionValues;

        // Loop over elements and process those that use the active basis
        for (int e = 0; e < elementCount; ++e) {
            if (basesAndCornerCounts[e].first != &activeBasis)
                continue;

            // Local coefficients of the argument in the current element
            const std::vector<ResultType>& activeLocalCoefficients =
                    localCoefficients[e];

            // Calculate the function's values and/or derivatives
            // at the requested points in the current element
            if (basisDeps & Fiber::VALUES) {
                functionData.values.fill(0.);
                for (int point = 0; point < basisData.values.n_slices; ++point)
                    for (int dim = 0; dim < basisData.values.n_rows; ++dim)
                        for (int fun = 0; fun < basisData.values.n_cols; ++fun)
                            functionData.values(dim, 0, point) +=
                                    basisData.values(dim, fun, point) *
                                    activeLocalCoefficients[fun];
            }
            if (basisDeps & Fiber::DERIVATIVES) {
                std::fill(functionData.derivatives.begin(),
                          functionData.derivatives.end(), 0.);
                for (int point = 0; point < basisData.derivatives.extent(3); ++point)
                    for (int dim = 0; dim < basisData.derivatives.extent(1); ++dim)
                        for (int comp = 0; comp < basisData.derivatives.extent(0); ++comp)
                            for (int fun = 0; fun < basisData.derivatives.extent(2); ++fun)
                                functionData.derivatives(comp, dim, 0, point) +=
                                        basisData.derivatives(comp, dim, fun, point) *
                                        activeLocalCoefficients[fun];
            }

            // Get geometrical data
            rawGeometry.setupGeometry(e, *geometry);
            geometry->getData(geomDeps, local, geomData);

            throw std::logic_error("NOT WORKING!!!");
            // expression.evaluate(functionData, geomData, functionValues);
            assert(functionValues.n_cols == 1);

            if (dataType == VtkWriter::CELL_DATA)
                result.col(e) = functionValues.slice(0);
            else { // VERTEX_DATA
                // Add the calculated values to the columns of the result array
                // corresponding to the active element's vertices
                for (int c = 0; c < activeCornerCount; ++c) {
                    int vertexIndex = rawGeometry.elementCornerIndices()(c, e);
                    result.col(vertexIndex) += functionValues.slice(c);
                    ++multiplicities[vertexIndex];
                }
            }
        } // end of loop over elements
    } // end of loop over unique combinations of basis and corner count

    // Take average of the vertex values obtained in each of the adjacent elements
    if (dataType == VtkWriter::VERTEX_DATA)
        for (int v = 0; v < vertexCount; ++v)
            result.col(v) /= multiplicities[v];
}

template <typename ArgumentType, typename ResultType>
GridFunction<ArgumentType, ResultType> operator+(
        const GridFunction<ArgumentType, ResultType>& g1,
        const GridFunction<ArgumentType, ResultType>& g2)
{
    if (&g1.space() != &g2.space())
        throw std::runtime_error("GridFunction::operator+(): spaces don't match");
    arma::Col<ResultType> g1Vals = g1.coefficients().asArmadilloVector();
    arma::Col<ResultType> g2Vals = g2.coefficients().asArmadilloVector();
    return GridFunction<ArgumentType, ResultType>(g1.space(), g1Vals + g2Vals);
}

template <typename ArgumentType, typename ResultType>
GridFunction<ArgumentType, ResultType> operator-(
        const GridFunction<ArgumentType, ResultType>& g1,
        const GridFunction<ArgumentType, ResultType>& g2)
{
    if (&g1.space() != &g2.space())
        throw std::runtime_error("GridFunction::operator-(): spaces don't match");
    arma::Col<ResultType> g1Vals = g1.coefficients().asArmadilloVector();
    arma::Col<ResultType> g2Vals = g2.coefficients().asArmadilloVector();
    return GridFunction<ArgumentType, ResultType>(g1.space(), g1Vals - g2Vals);
}

template <typename ArgumentType, typename ResultType, typename ScalarType>
GridFunction<ArgumentType, ResultType> operator*(
        const GridFunction<ArgumentType, ResultType>& g1, const ScalarType& scalar)
{
    arma::Col<ResultType> g1Vals = g1.coefficients().asArmadilloVector();
    return GridFunction<ArgumentType, ResultType>(g1.space(), scalar * g1Vals);
}

template <typename ArgumentType, typename ResultType, typename ScalarType>
GridFunction<ArgumentType, ResultType> operator*(
        const ScalarType& scalar, const GridFunction<ArgumentType, ResultType>& g2)
{
    return g2 * scalar;
}

template <typename ArgumentType, typename ResultType, typename ScalarType>
GridFunction<ArgumentType, ResultType> operator/(
        const GridFunction<ArgumentType, ResultType>& g1, const ScalarType& scalar)
{
    if (scalar==0) throw std::runtime_error("Divide by zero");
    return (static_cast<ScalarType>(1.) / scalar) * g1;
}

FIBER_INSTANTIATE_CLASS_TEMPLATED_ON_BASIS_AND_RESULT(GridFunction);

//#ifdef ENABLE_SINGLE_PRECISION
//template class GridFunction<float, float>;
//template GridFunction<float, float> operator+(
//const GridFunction<float, float>& g1, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator-(
//const GridFunction<float, float>& g1, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator*(
//const GridFunction<float, float>& g1, const float& scalar);
//template GridFunction<float, float> operator*(
//const GridFunction<float, float>& g1, const double& scalar);
//template GridFunction<float, float> operator*(
//const float& scalar, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator*(
//const double& scalar, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator/(
//const GridFunction<float, float>& g1, const float& scalar);
//template GridFunction<float, float> operator/(
//const GridFunction<float, float>& g1, const double& scalar);
//#  ifdef ENABLE_COMPLEX_KERNELS
//#  include <complex>
//template class GridFunction<float, std::complex<float> >;

//template GridFunction<float, std::complex<float> > operator+(
//const GridFunction<float, std::complex<float> >& g1,
//const GridFunction<float, std::complex<float> >& g2);
//template GridFunction<float, std::complex<float> > operator-(
//const GridFunction<float, std::complex<float> >& g1,
//const GridFunction<float, std::complex<float> >& g2);

//template GridFunction<float, std::complex<float> > operator*(
//const GridFunction<float, std::complex<float> >& g1, const float& scalar);
//template GridFunction<float, std::complex<float> > operator*(
//const GridFunction<float, std::complex<float> >& g1, const double& scalar);
//template GridFunction<float, std::complex<float> > operator*(
//const GridFunction<float, std::complex<float> >& g1, const std::complex<float>& scalar);
//template GridFunction<float, std::complex<float> > operator*(
//const GridFunction<float, std::complex<float> >& g1, const std::complex<double>& scalar);

//template GridFunction<float, std::complex<float> > operator*(
//const float& scalar, const GridFunction<float, std::complex<float> >& g2);
//template GridFunction<float, std::complex<float> > operator*(
//const double& scalar, const GridFunction<float, std::complex<float> >& g2);
//template GridFunction<float, std::complex<float> > operator*(
//const std::complex<float>& scalar, const GridFunction<float, std::complex<float> >& g2);
//template GridFunction<float, std::complex<float> > operator*(
//const std::complex<double>& scalar, const GridFunction<float, std::complex<float> >& g2);

//template GridFunction<float, std::complex<float> > operator/(
//const GridFunction<float, std::complex<float> >& g1, const float& scalar);
//template GridFunction<float, std::complex<float> > operator/(
//const GridFunction<float, std::complex<float> >& g1, const double& scalar);
//template GridFunction<float, std::complex<float> > operator/(
//const GridFunction<float, std::complex<float> >& g1, const std::complex<float>& scalar);
//template GridFunction<float, std::complex<float> > operator/(
//const GridFunction<float, std::complex<float> >& g1, const std::complex<double>& scalar);
//#    ifdef ENABLE_COMPLEX_BASIS_FUNCTIONS
//template class GridFunction<std::complex<float>, std::complex<float> >;

//#    endif // !ENABLE_COMPLEX_BASIS_FUNCTIONS
//#  else // !ENABLE_COMPLEX_KERNELS
//#    ifdef ENABLE_COMPLEX_BASIS_FUNCTIONS
//#    include <complex>
//template class GridFunction<std::complex<float>, float>;
//template GridFunction<float, float> operator+(
//const GridFunction<float, float>& g1, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator-(
//const GridFunction<float, float>& g1, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator*(
//const GridFunction<float, float>& g1, const float& scalar);
//template GridFunction<float, float> operator*(
//const GridFunction<float, float>& g1, const double& scalar);
//template GridFunction<float, float> operator*(
//const float& scalar, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator*(
//const double& scalar, const GridFunction<float, float>& g2);
//template GridFunction<float, float> operator/(
//const GridFunction<float, float>& g1, const float& scalar);
//template GridFunction<float, float> operator/(
//const GridFunction<float, float>& g1, const double& scalar);
//#    endif // !ENABLE_COMPLEX_BASIS_FUNCTIONS
//#  endif // !ENABLE_COMPLEX_KERNELS
//#endif // ENABLE_SINGLE_PRECISION

//#ifdef ENABLE_DOUBLE_PRECISION
//template class GridFunction<double, double>;
//template GridFunction<double, double> operator+(
//const GridFunction<double, double>& g1, const GridFunction<double, double>& g2);
//template GridFunction<double, double> operator-(
//const GridFunction<double, double>& g1, const GridFunction<double, double>& g2);
//template GridFunction<double, double> operator*(
//const GridFunction<double, double>& g1, const float& scalar);
//template GridFunction<double, double> operator*(
//const GridFunction<double, double>& g1, const double& scalar);
//template GridFunction<double, double> operator*(
//const float& scalar, const GridFunction<double, double>& g2);
//template GridFunction<double, double> operator*(
//const double& scalar, const GridFunction<double, double>& g2);
//template GridFunction<double, double> operator/(
//const GridFunction<double, double>& g1, const float& scalar);
//template GridFunction<double, double> operator/(
//const GridFunction<double, double>& g1, const double& scalar);
//#  ifdef ENABLE_COMPLEX_KERNELS
//#  include <complex>
//template class GridFunction<double, std::complex<double> >;
//#    ifdef ENABLE_COMPLEX_BASIS_FUNCTIONS
//template class GridFunction<std::complex<double>, std::complex<double> >;
//#    endif // !ENABLE_COMPLEX_BASIS_FUNCTIONS
//#  else // !ENABLE_COMPLEX_KERNELS
//#    ifdef ENABLE_COMPLEX_BASIS_FUNCTIONS
//#    include <complex>
//template class GridFunction<std::complex<double>, double>;
//#    endif // !ENABLE_COMPLEX_BASIS_FUNCTIONS
//#  endif // !ENABLE_COMPLEX_KERNELS
//#endif

//#ifdef COMPILE_FOR_FLOAT
//template class GridFunction<float>;
//template GridFunction<float> operator+(const GridFunction<float>& g1, const GridFunction<float>& g2);
//template GridFunction<float> operator-(const GridFunction<float>& g1, const GridFunction<float>& g2);
//template GridFunction<float> operator*(const GridFunction<float>& g1, const float& scalar);
//template GridFunction<float> operator*(const float& scalar, const GridFunction<float>& g2);
//template GridFunction<float> operator/(const GridFunction<float>& g1, const float& scalar);

//#endif
//#ifdef COMPILE_FOR_DOUBLE
//template class GridFunction<double>;
//template GridFunction<double> operator+(const GridFunction<double>& g1, const GridFunction<double>& g2);
//template GridFunction<double> operator-(const GridFunction<double>& g1, const GridFunction<double>& g2);
//template GridFunction<double> operator*(const GridFunction<double>& g1, const double& scalar);
//template GridFunction<double> operator*(const double& scalar, const GridFunction<double>& g2);
//template GridFunction<double> operator/(const GridFunction<double>& g1, const double& scalar);

//#endif
//#ifdef COMPILE_FOR_COMPLEX_FLOAT
//#include <complex>
//template class GridFunction<std::complex<float> >;
//template GridFunction<std::complex<float> > operator+(const GridFunction<std::complex<float> >& g1, const GridFunction<std::complex<float> >& g2);
//template GridFunction<std::complex<float> > operator-(const GridFunction<std::complex<float> >& g1, const GridFunction<std::complex<float> >& g2);
//template GridFunction<std::complex<float> > operator*(const GridFunction<std::complex<float> >& g1, const std::complex<float> & scalar);
//template GridFunction<std::complex<float> > operator*(const std::complex<float> & scalar, const GridFunction<std::complex<float> >& g2);
//template GridFunction<std::complex<float> > operator/(const GridFunction<std::complex<float> >& g1, const std::complex<float> & scalar);

//#endif
//#ifdef COMPILE_FOR_COMPLEX_DOUBLE
//#include <complex>
//template class GridFunction<std::complex<double> >;
//template GridFunction<std::complex<double> > operator+(const GridFunction<std::complex<double> >& g1, const GridFunction<std::complex<double> >& g2);
//template GridFunction<std::complex<double> > operator-(const GridFunction<std::complex<double> >& g1, const GridFunction<std::complex<double> >& g2);
//template GridFunction<std::complex<double> > operator*(const GridFunction<std::complex<double> >& g1, const std::complex<double> & scalar);
//template GridFunction<std::complex<double> > operator*(const std::complex<double> & scalar, const GridFunction<std::complex<double> >& g2);
//template GridFunction<std::complex<double> > operator/(const GridFunction<std::complex<double> >& g1, const std::complex<double> & scalar);

//#endif

} // namespace Bempp

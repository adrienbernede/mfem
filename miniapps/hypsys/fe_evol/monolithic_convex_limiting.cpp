#include "monolithic_convex_limiting.hpp"

MCL_Evolution::MCL_Evolution(FiniteElementSpace *fes_,
                             HyperbolicSystem *hyp_,
                             DofInfo &dofs_)
   : FE_Evolution(fes_, hyp_, dofs_)
{
   Mesh *mesh = fes->GetMesh();
   const FiniteElement *el = fes->GetFE(0);
   IntegrationRule nodes =  el->GetNodes();
   Vector shape(nd), dx_shape(nd);
   DenseMatrix dshape(nd,dim), GradOp(nd,nd), test(nd,dim);

   ShapeEval.SetSize(nd,nd);
   ElemInt.SetSize(dim,dim,ne);
   PrecGrad.SetSize(nd,dim,nd);
   OuterUnitNormals.SetSize(dim, dofs.NumBdrs, ne);

   PrecGrad = 0.;
   test = 0.;

   Array <int> bdrs, orientation;

   for (int j = 0; j < nd; j++)
   {
      const IntegrationPoint &ip = nodes.IntPoint(j);
      el->CalcShape(ip, shape);
      ShapeEval.SetCol(j, shape);
   }

   for (int k = 0; k < nqe; k++)
   {
      const IntegrationPoint &ip = IntRuleElem->IntPoint(k);
      el->CalcShape(ip, shape);
      el->CalcDShape(ip, dshape);

      for (int l = 0; l < dim; l++)
      {
         GradOp = 0.;
         dshape.GetColumn(l, dx_shape);
         AddMult_a_VWt(ip.weight, shape, dx_shape, GradOp);

         for (int i = 0; i < nd; i++)
         {
            for (int j = 0; j < nd; j++)
            {
               PrecGrad(i,l,j) -= GradOp(i,j);
               test(i,l) += GradOp(i,j);
            }
         }
      }
   }

   // test.Print();

   for (int e = 0; e < ne; e++)
   {
      ElementTransformation *eltrans = fes->GetElementTransformation(e);
      for (int k = 0; k < nqe; k++)
      {
         const IntegrationPoint &ip = IntRuleElem->IntPoint(k);
         eltrans->SetIntPoint(&ip);
         CalcAdjugate(eltrans->Jacobian(), ElemInt(e));
      }

      if (dim==1)      { mesh->GetElementVertices(e, bdrs); }
      else if (dim==2) { mesh->GetElementEdges(e, bdrs, orientation); }
      else if (dim==3) { mesh->GetElementFaces(e, bdrs, orientation); }

      for (int i = 0; i < dofs.NumBdrs; i++)
      {
         Vector nor(dim);
         FaceElementTransformations *facetrans
            = mesh->GetFaceElementTransformations(bdrs[i]);

         const IntegrationPoint &ip = IntRuleFace->IntPoint(0);
         facetrans->Face->SetIntPoint(&ip);

         if (dim == 1)
         {
            IntegrationPoint aux;
            facetrans->Loc1.Transform(ip, aux);
            nor(0) = 2.*aux.x - 1.0;
         }
         else
         {
            CalcOrtho(facetrans->Face->Jacobian(), nor);
         }

         if (facetrans->Elem1No != e)
         {
            nor *= -1.;
         }

         nor /= nor.Norml2();
         for (int l = 0; l < dim; l++)
         {
            OuterUnitNormals(l,i,e) = nor(l);
         }
      }
   }
}

void MCL_Evolution::Mult(const Vector &x, Vector &y) const
{
   ComputeTimeDerivative(x, y);
}

void MCL_Evolution::ComputeTimeDerivative(const Vector &x, Vector &y,
                                          const Vector &xMPI) const
{
   if (hyp->TimeDepBC)
   {
      hyp->BdrCond.SetTime(t);
      if (!hyp->ProjType)
      {
         hyp->L2_Projection(hyp->BdrCond, inflow);
      }
      else
      {
         inflow.ProjectCoefficient(hyp->BdrCond);
      }
   }

   z = 0.;
   for (int e = 0; e < ne; e++)
   {
      fes->GetElementVDofs(e, vdofs);
      x.GetSubVector(vdofs, uElem);
      mat2 = 0.;

      for (int j = 0; j < nd; j++)
      {
         ElemEval(uElem, uEval, j);
         hyp->EvaluateFlux(uEval, Flux, e, j);

         MultABt(ElemInt(e), Flux, mat1);
         AddMult(PrecGrad(j), mat1, mat2);
      }

      z.AddElementVector(vdofs, mat2.GetData());

      // Here, the use of nodal basis functions is essential, i.e. shape
      // functions must vanish on faces that their node is not associated with.
      for (int i = 0; i < dofs.NumBdrs; i++)
      {
         OuterUnitNormals(e).GetColumn(i, normal);

         for (int j = 0; j < dofs.NumFaceDofs; j++)
         {
            double c_eij = 0.;
            Vector C_eij = normal;
            for (int k = 0; k < nqf; k++)
            {
               c_eij += BdrInt(i,k,e) * ShapeEvalFace(i,j,k);
            }

            C_eij *= c_eij;

            for (int n = 0; n < hyp->NumEq; n++)
            {
               nbr = dofs.NbrDofs(i,j,e);
               DofInd = n * ne * nd + e * nd + dofs.BdrDofs(j,i);

               if (nbr < 0)
               {
                  uNbr = inflow(DofInd);
               }
               else
               {
                  // nbr in different MPI task?
                  uNbr = (nbr < xSizeMPI) ? x(n * ne * nd + nbr) :
                         xMPI(int((nbr - xSizeMPI) / nd) * nd * hyp->NumEq + n * nd +
                              (nbr - xSizeMPI) % nd);
               }

               uEval(n) = x(DofInd);
               uNbrEval(n) = uNbr;
            }

            if (nbr < 0)
            {
               hyp->SetBdrCond(uEval, uNbrEval, normal, nbr);
            }

            double ws = max( hyp->GetWaveSpeed(uEval, normal, e, j, i),
                             hyp->GetWaveSpeed(uNbrEval, normal, e, j, i) );

            c_eij *= ws;

            hyp->EvaluateFlux(uEval, Flux, e, j, i);
            hyp->EvaluateFlux(uNbrEval, FluxNbr, e, j, i);
            Flux -= FluxNbr;
            Vector tmp(hyp->NumEq);
            Flux.Mult(C_eij, tmp);

            for (int n = 0; n < hyp->NumEq; n++)
            {
               z(vdofs[n * nd + dofs.BdrDofs(j,i)]) + 0.5 * (
                  (uNbrEval(n) -uEval(n)) * c_eij + tmp(n) );
            }
         }
      }

      for (int n = 0; n < hyp->NumEq; n++)
      {
         for (int j = 0; j < nd; j++)
         {
            DofInd = n*ne*nd + e*nd + j;
            y(DofInd) = z(DofInd) / LumpedMassMat(DofInd);
         }
      }
   }
}
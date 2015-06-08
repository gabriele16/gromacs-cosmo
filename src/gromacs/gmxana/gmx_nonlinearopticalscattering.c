 /*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "vec.h"
#include "pbc.h"
#include "xvgr.h"
#include "gromacs/fileio/futil.h"
#include "gromacs/commandline/pargs.h"
#include "gromacs/fileio/tpxio.h"
#include "gromacs/fileio/trxio.h"
#include "physics.h"
#include "index.h"
#include "gromacs/utility/smalloc.h"
#include "calcgrid.h"
#include "nrnb.h"
#include "coulomb.h"
#include "gstat.h"
#include "gromacs/fileio/matio.h"
#include "gmx_ana.h"
#include "hyperpol.h"
#include "names.h"

#include "gromacs/legacyheaders/gmx_fatal.h"

static void do_nonlinearopticalscattering(t_topology *top, /*const char *fnNDX, const char *fnTPS,*/ const char *fnTRX,
                   const char *fnSFACT, const char *fnOSRDF, const char *fnORDF, /*const char *fnHQ, */
                   const char *method,
                   gmx_bool bPBC, gmx_bool bNormalize, gmx_bool bKleinmannsymm, gmx_bool bSpectrum,
                   real maxq,  int nbinq, real kx, real ky, real kz, 
                   int p_out, int p_in1, int p_in2 ,real binwidth,
                   real fade, real faderdf, int *isize, int  *molindex[], char **grpname, int ng,
                   const output_env_t oenv)
{
    FILE          *fp;
    FILE          *fpn;
    t_trxstatus   *status;
    char           outf1[STRLEN], outf2[STRLEN];
    char           title[STRLEN], gtitle[STRLEN], refgt[30];
    int            g, natoms, i, j, k, nbin, qq, n, nframes;
    int          **count;
    real         **s_method, **s_method_coh, *s_method_nospectrum, *temp_method, *s_method_coh_nospectrum, s_method_coh_temp = 0.0;
    real         **s_method_g_r, *analytical_integral, *arr_q, qel, minq;
    real          *cos_q, *sin_q, ***beta_mol, *beta_lab, s_method_incoh = 0.0, beta_lab_sq_t = 0.0, *beta_lab_sq_try , beta_lab_t = 0.0 ,cos_c = 0.0, sin_c = 0.0;
    int            nrdf = 0, max_i, isize0, ind0;
    real           t, rmax2, rmax,  r, r_dist, r2, q_xi, dq, invhbinw, normfac, norm_x, norm_z, mod_f, inv_width, bl = 0.0,  bsq = 0.0;
    real           segvol, spherevol, prev_spherevol, **rdf, invsize0;
    rvec          *x, dx,  *x_i1, xi, x01, x02, arr_qvec, pol_out, pol_in1, pol_in2; 
    real          *inv_segvol, invvol, invvol_sum, rho;
    matrix         box, box_pbc;
    int            ePBC = -1, ePBCrdf = -1;
    t_block       *mols = NULL;
    t_atom        *atom = NULL;
    t_pbc          pbc;
    gmx_rmpbc_t    gpbc = NULL;
    int            mol, a;

    atom = top->atoms.atom;
    mols = &(top->mols);
    isize0 = isize[0];
    invsize0 = 1.0/isize0;
    natoms = read_first_x(oenv, &status, fnTRX, &t, &x, box);
    
    fprintf(stderr,"\nnatoms %d\n",natoms);
    fprintf(stderr,"molindex %d\n",molindex[0][0]);
    fprintf(stderr,"x[0] %f %f %f\n", x[0][XX], x[0][YY], x[0][ZZ]);
    fprintf(stderr,"isize[0] %d isize[1] %d, isize[2] %d, invsize0 %f \n",isize[0], isize[1], isize[2], invsize0);
    fprintf(stderr,"ng %d\n",ng);

    /* initialize some handy things */
    if (ePBC == -1)
    {
        ePBC = guess_ePBC(box);
    }
    copy_mat(box, box_pbc);
    ePBCrdf = ePBC;
    if (bPBC)
    {
        rmax2   =  max_cutoff2(FALSE ? epbcXY : epbcXYZ, box_pbc);
        fprintf(stderr, "rmax2 = %f\n", rmax2);
        
    }
    else
    {
        rmax2   = sqr(3*max(box[XX][XX], max(box[YY][YY], box[ZZ][ZZ])));
    }
    if (debug)
    {
        fprintf(debug, "rmax2 = %g\n", rmax2);
    }

    set_pbc(&pbc, ePBCrdf, box_pbc);
    ind0  = mols->index[molindex[0][0]];
    pbc_dx(&pbc, x[ind0], x[ind0+1], x01);
    pbc_dx(&pbc, x[ind0], x[ind0+2], x02);
    rvec_sub( x01, x02, xi);
    fprintf(stderr,"xvec %f %f %f \n",xi[XX], xi[YY], xi[ZZ]);
    norm_x = gmx_invsqrt(norm2(xi));
    rvec_add( x01, x02, xi);
    fprintf(stderr,"xvec %f %f %f \n",xi[XX], xi[YY], xi[ZZ]);
    norm_z = gmx_invsqrt(norm2(xi));
    fprintf(stderr,"norm_z norm_x %f %f \n", norm_x, norm_z);
    /* We use the double amount of bins, so we can correctly
     * write the rdf and rdf_cn output at i*binwidth values.
     */
    nbin     = (int)(sqrt(rmax2) * 2 / binwidth);
    invhbinw = 2.0 / binwidth;
    rmax     = sqrt(rmax2);

    snew(count, ng);
    snew(s_method, ng);
    snew(s_method_coh, ng);
    snew(s_method_nospectrum, ng);
    snew(s_method_coh_nospectrum, ng);
    snew(s_method_g_r, ng);
    max_i = isize[0];

    /*allocate memory for beta in lab frame and initialize beta in mol frame*/
    snew(beta_lab, isize[0]);
    snew(beta_lab_sq_try, isize[0]);
    snew(beta_mol, DIM);
    for (i = 0; i < DIM; i++)
    {
        snew(beta_mol[i], DIM);
    }
    for (i = 0; i < DIM; i++)
    {    
        for (j = 0; j < DIM; j++)
        {
            snew(beta_mol[i][j], DIM);
        }
    }

    /*beta_mol parameters in a.u. taken from Kusalik Mol. Phys. 99 1107-1120, (2001)*/
    /*convert from a.u. to [e^3]*[nm^3]/[Hartree^2]*/
    /* use the fact that beta_ijj = beta_jij */
    beta_mol[2][0][0] = 5.7/**0.000148184711*/;
    beta_mol[2][2][2] = 31.6/**0.000148184711*/;
    beta_mol[2][1][1] = 10.9/**0.000148184711*/;
    fprintf(stderr,"beta_mol[2][0][0] %f\n",beta_mol[2][0][0]);
    fprintf(stderr,"beta_mol[2][2][2] %f\n",beta_mol[2][2][2]);
    fprintf(stderr,"beta_mol[2][1][1] %f\n",beta_mol[2][1][1]);
    if (bKleinmannsymm)
    {
       beta_mol[0][0][2] = 5.7 ;
       beta_mol[0][2][0] = 5.7 ;
       beta_mol[1][1][2] = 10.9;
       beta_mol[1][2][1] = 10.9;
    }
    
    for (g = 0; g < ng; g++)
    {
        /* this is THE array */
        snew(count[g], nbin+1);
        /*allocate memory for s_method array */
        snew(s_method[g], nbinq);
        snew(s_method_coh[g], nbinq);
        snew(s_method_g_r[g], nbinq);
        snew(temp_method, nbinq);
        snew(arr_q,nbinq);
        snew(cos_q,nbinq);
        snew(sin_q,nbinq);
        snew(analytical_integral,nbinq);
        normfac = 1.0/sqrt(kx*kx + ky*ky + kz*kz) ;
        arr_qvec[XX] = kx*normfac;
        arr_qvec[YY] = ky*normfac;
        arr_qvec[ZZ] = kz*normfac;
        /*initialize polarization vectors*/
        for (i = 0 ; i<DIM; i++)
        {
          pol_out[i] = 0.0;
          pol_in1[i] = 0.0;
          pol_in2[i] = 0.0;
        }
        /*assign the non-zero components of the polarization vectors to project beta to the lab-frame*/
        /* e.g. beta_zzz is obtained with pol_out[2] = 1.0, pol_in1[2] = 1.0, pol_in2[2] = 1.0 */
        pol_out[p_out] = 1.0;
        pol_in1[p_in1] = 1.0;
        pol_in2[p_in2] = 1.0;
        inv_width = (fade == 0.0 ) ? 1.0 : M_PI*0.5/(rmax-fade) ;
        minq     = sqrt(2)*2*M_PI/rmax ; 
        dq=(maxq-minq)/nbinq ;       
        for (qq = 0; qq< nbinq; qq++)
        {
            arr_q[qq]=minq+dq*qq;
            if (fade == 0.0)
            {
               analytical_integral[qq]=((sin(arr_q[qq]*rmax) - arr_q[qq]*rmax*cos(arr_q[qq]*rmax))/(arr_q[qq]*arr_q[qq]*arr_q[qq]))*4.0*M_PI*isize0;
            }
            else
            {
               qel = arr_q[qq] ;
               analytical_integral[qq] = 4.0*M_PI*isize0*(-fade*qel*cos(fade*qel) + sin(fade*qel))/pow(qel,3.0); 
               analytical_integral[qq] +=  4.0*M_PI*isize0*((fade*qel*(M_PI + qel*(fade - rmax))*(pow(M_PI,2) - 2*pow(qel,2)*pow(fade - rmax,2))*\
               (M_PI + qel*(-fade + rmax))*cos(fade*qel) -\
               pow(M_PI,2)*qel*(M_PI + qel*(fade - rmax))*rmax*(M_PI + qel*(-fade + rmax))*cos(qel*rmax) + \
               (-pow(M_PI,4) + pow(M_PI,2)*pow(qel,2)*pow(fade - rmax,2) - 2*pow(qel,4)*pow(fade - rmax,4))*sin(fade*qel) + \
               pow(M_PI,2)*(pow(M_PI,2) - 3*pow(qel,2)*pow(fade - rmax,2))*sin(qel*rmax))/\
               (2.*pow(qel,3)*pow(M_PI + qel*(fade - rmax),2)*pow(M_PI + qel*(-fade + rmax),2)));
            }                                                                 
        }
    }

    snew(x_i1, max_i);
    nframes    = 0;
    invvol_sum = 0;
    if (bPBC && (NULL != top))
    {
        gpbc = gmx_rmpbc_init(&top->idef, ePBC, natoms);
    }
    if (method[0] == 'm' && bSpectrum == TRUE && fade == 0.0)
    {
       fprintf(stderr,"modified direct method (modsumexp)\n");
       do
       {
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);

            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                beta_lab_sq_t = 0.0;
                snew(temp_method, nbinq);
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z, x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            mod_f = beta_lab[i]*beta_lab[j]  ;
                            for (qq = 0; qq < nbinq; qq++)
                            {
                               /*s_method[g][qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx))  ;*/
                               /*fprintf(stderr,"s_method[g][qq] %f\n", mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx)) );*/
                               temp_method[qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx));
                            }
                        }
                    }
                }
                s_method_incoh += beta_lab_sq_t*invsize0 ;
                for (qq = 0; qq < nbinq; qq++)
                {
                   s_method_coh[g][qq] += 2.0*temp_method[qq]*invsize0 ;
                   s_method[g][qq] += (2.0*temp_method[qq] + beta_lab_sq_t)*invsize0 ;
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }

    if (method[0] == 'm' && bSpectrum == TRUE && fade != 0.0)
    {
       fprintf(stderr,"modified direct method (modsumexp) with fading and spectrum\n");
       do
       {
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);

            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                beta_lab_sq_t = 0.0;
                snew(temp_method, nbinq);
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z, x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            r_dist = sqrt(r2);
                            count[g][(int)(r_dist*invhbinw)] ++;
                            if (fade <= r_dist)
                            {
                                mod_f = beta_lab[i]*beta_lab[j]  ;
                                for (qq = 0; qq < nbinq; qq++)
                                {
                                   /*s_method[g][qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx))  ;*/
                                   /*fprintf(stderr,"s_method[g][qq] %f\n", mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx)) );*/
                                   temp_method[qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx));
                                }
                            }
                            else
                            {
                                mod_f = beta_lab[i]*beta_lab[j]*sqr(cos((r_dist-fade)*inv_width)) ;
                                for (qq = 0; qq < nbinq; qq++)
                                {
                                   /*s_method[g][qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx))  ;*/
                                   /*fprintf(stderr,"s_method[g][qq] %f\n", mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx)) );*/
                                   temp_method[qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx));
                                }
                            }
                        }
                    }
                }
                s_method_incoh += beta_lab_sq_t*invsize0 ;
                for (qq = 0; qq < nbinq; qq++)
                {
                   s_method_coh[g][qq] += 2.0*temp_method[qq]*invsize0 ;
                   s_method[g][qq] += (2.0*temp_method[qq] + beta_lab_sq_t)*invsize0 ;
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    if (method[0] == 'c' && bSpectrum == TRUE && fade == 0.0)
    {
        fprintf(stderr,"cosmo method, compute spectrum and no smoothing\n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
    
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
    
            for (g = 0; g < ng; g++)
            {
                beta_lab_sq_t = 0.0;
                snew(temp_method, nbinq);
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]]; 
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z, x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {   
                            r_dist = sqrt(r2);
                            count[g][(int)(r_dist*invhbinw)] ++;
                            mod_f = beta_lab[i]*beta_lab[j]/r_dist  ;
                            for (qq = 0; qq < nbinq; qq++)
                            {
                               /*s_method[g][qq] += mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx))  ;*/
                               /*fprintf(stderr,"s_method[g][qq] %f\n", mod_f*cos((minq+dq*qq)*iprod(arr_qvec,dx)) );*/
                               temp_method[qq] += mod_f*sin(arr_q[qq]*r_dist);
                            }
                        }
                    }
                }
                s_method_incoh += beta_lab_sq_t*invsize0 ;
                for (qq = 0; qq < nbinq; qq++)
                {
                   s_method_coh[g][qq] += 2.0*invsize0*temp_method[qq]/arr_q[qq] ;
                   s_method[g][qq] += invsize0*(2.0*temp_method[qq]/arr_q[qq] + beta_lab_sq_t) ;
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    else if (method[0] == 'c' && bSpectrum == FALSE && fade == 0.0)
    {
        fprintf(stderr,"cosmo method, don't compute spectrum and no smoothing\n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            beta_lab_sq_t = 0.0 ;
            s_method_coh_temp = 0.0;
            for (g = 0; g < ng; g++)
            {
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);

                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z,  x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                }
                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        pbc_dx(&pbc, xi, x_i1[j], dx);
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            r_dist = sqrt(r2);
                            count[g][(int)(r_dist*invhbinw)] ++;
                            s_method_coh_temp += beta_lab[i]*beta_lab[j]*sin(minq*r_dist)/(minq*r_dist);
                            /*fprintf(stderr,"%f",s_method_nospectrum[g]);*/
                        }
                    }
                }
                s_method_coh_nospectrum[g] += s_method_coh_temp*invsize0*2.0 ; 
                s_method_incoh += beta_lab_sq_t*invsize0 ;
                s_method_nospectrum[g] += (2.0*s_method_coh_temp + beta_lab_sq_t)*invsize0 ;
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    if (method[0] == 'c' && bSpectrum == FALSE && fade != 0.0)
    {
        fprintf(stderr,"cosmo method, don't compute spectrum but smoothen curve\n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            beta_lab_sq_t = 0.0 ;
            for (g = 0; g < ng; g++)
            {
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], x_i1[i]);
                    pbc_dx(&pbc, x_i1[i], x[ind0+1], x01);
                    pbc_dx(&pbc, x_i1[i], x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z,  x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                }

                for (i = 0; i < isize0 -1.0; i++)
                {
                    /* Real rdf between points in space */
                    ind0 = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    for (j = i + 1; j < isize0 ; j++)
                    {
                        if (bPBC)
                        {
                            pbc_dx(&pbc, xi, x_i1[j], dx);
                        }
                        else
                        {
                            rvec_sub(xi, x_i1[j], dx);
                        }
                        r2 = iprod(dx, dx);
                        if (r2 <= rmax2 )
                        {
                            r_dist = sqrt(r2);
                            count[g][(int)(r_dist*invhbinw)] ++;
                            if (r_dist <= fade)
                            {
                                s_method_nospectrum[g] += 2.0*invsize0*beta_lab[i]*beta_lab[j]*sin(minq*r_dist)/(r_dist*minq);
                            }
                            else
                            {
                                mod_f = beta_lab[i]*beta_lab[j]*sqr(cos((r_dist-fade)*inv_width)) ;
                                s_method_nospectrum[g] += 2.0*invsize0*mod_f*sin(minq*r_dist)/(r_dist*minq);
                            }
                        }
                    }
                }
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    else if (method[0] == 's' && bSpectrum == TRUE)
    {   
        fprintf(stderr,"loop with sumexp method and spectrum\n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                snew(cos_q,nbinq);
                snew(sin_q,nbinq);
                beta_lab_sq_t = 0.0;
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    pbc_dx(&pbc, xi, x[ind0+1], x01);
                    pbc_dx(&pbc, xi, x[ind0+2], x02);
                    beta_lab[i] = rotate_beta(norm_x, norm_z, x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    
                    beta_lab_sq_t += sqr(beta_lab[i]);
                    for (qq = 0; qq < nbinq; qq++)
                    {
                        q_xi = (minq+dq*qq)*iprod(arr_qvec,xi);
                        cos_q[qq] += beta_lab[i]*cos(q_xi);
                        sin_q[qq] += beta_lab[i]*sin(q_xi);
                    }
                }
                s_method_incoh += beta_lab_sq_t*invsize0;
                /*if (s_method_incoh < 0.0 || beta_lab_sq_t < 0.0)
                {
                   gmx_fatal(FARGS,"s_method_incoh %f beta_lab_sq_t %f  nframes %d\n", s_method_incoh,beta_lab_sq_t,nframes);
                }*/
                for (qq = 0; qq < nbinq; qq++)
                {
                    s_method[g][qq] += (sqr(cos_q[qq]) + sqr(sin_q[qq]))*invsize0;
                    s_method_coh[g][qq] += (sqr(cos_q[qq]) + sqr(sin_q[qq]) - beta_lab_sq_t)*invsize0;
                }
                sfree(cos_q);
                sfree(sin_q);
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    else if (method[0] == 's' && bSpectrum == FALSE)
    {
        fprintf(stderr,"loop with sumexp method without computing spectrum\n");
        do
        {
            /* Must init pbc every step because of pressure coupling */
            copy_mat(box, box_pbc);
            if (top != NULL)
            {
                gmx_rmpbc(gpbc, natoms, box, x);
            }
            set_pbc(&pbc, ePBCrdf, box_pbc);
            invvol      = 1/det(box_pbc);
            invvol_sum += invvol;
            for (g = 0; g < ng; g++)
            {
                cos_c = 0.0; 
                sin_c = 0.0;
                beta_lab_sq_t = 0.0;
                for (i = 0; i < isize0; i++)
                {
                    ind0  = mols->index[molindex[g][i]];
                    copy_rvec(x[ind0], xi);
                    pbc_dx(&pbc, xi, x[ind0+1], x01);
                    pbc_dx(&pbc, xi, x[ind0+2], x02);
                    beta_lab_t = rotate_beta(norm_x, norm_z, x01, x02, pol_out, pol_in1, pol_in2, beta_mol );
                    beta_lab_sq_t += sqr(beta_lab[i]);
                    q_xi = minq*iprod(arr_qvec,xi); 
                    cos_c += beta_lab_t*cos(q_xi);
                    sin_c += beta_lab_t*sin(q_xi);
                }
                s_method_nospectrum[g] += (cos_c*cos_c + sin_c*sin_c)*invsize0   ;
                s_method_coh_nospectrum[g] += (cos_c*cos_c + sin_c*sin_c  - beta_lab_sq_t)*invsize0  ;
                s_method_incoh += beta_lab_sq_t*invsize0;
            }
            nframes++;
        }
        while (read_next_x(oenv, status, &t, x, box));
    }
    else if ((method[0] == 'c' && bSpectrum == TRUE && fade != 0.0) || (method[0] == 's' && fade != 0.0) || (method[0] == 'm' && fade != 0.0) )
    {
        gmx_fatal(FARGS,"Combination of method %c, spectrum %d and fading %d  unavailable", method[0], bSpectrum, fade);
    }
    fprintf(stderr, "\n");
    if (bPBC && (NULL != top))
    {
        gmx_rmpbc_done(gpbc);
    }
    
    close_trj(status);

    sfree(x);

    /* Average volume */
    invvol = invvol_sum/nframes;
    if (method[0]=='c' || method[0]=='m')
    {
       /* Calculate volume of sphere segments or length of circle segments */
       snew(inv_segvol, (nbin+1)/2);
       prev_spherevol = 0;
       for (i = 0; (i < (nbin+1)/2); i++)
       {
           r = (i + 0.5)*binwidth;
           spherevol = (4.0/3.0)*M_PI*r*r*r;
           segvol         = spherevol-prev_spherevol;
           inv_segvol[i]  = 1.0/segvol;
           prev_spherevol = spherevol;
       }
   
       snew(rdf, ng);
       for (g = 0; g < ng; g++)
       {
           /* We have to normalize by dividing by the number of frames */
           normfac = 2.0/(nframes*invvol*isize0*isize0);
           /* Do the normalization */
           nrdf = max((nbin+1)/2, 1+2*faderdf/binwidth);
           snew(rdf[g], nrdf);
           for (i = 0; i < (nbin+1)/2; i++)
           {
               r = i*binwidth;
               if (i == 0)
               {
                   j = count[g][0];
               }
               else
               {
                   j = count[g][i*2-1] + count[g][i*2];
               }
               if ( (faderdf > 0) && (r >= faderdf) )
               {
                   rdf[g][i] = 1 + (j*inv_segvol[i]*normfac-1)*exp(-16*sqr(r/faderdf-1));
               }
               else
               {
                   if (bNormalize)
                   {
                       rdf[g][i] = j*inv_segvol[i]*normfac;
                   }
                   else
                   {
                       rdf[g][i] = j/(binwidth*isize0*nframes);
                   }
               }
           }
           for (; (i < nrdf); i++)
           {
               rdf[g][i] = 1.0;
           }
       }
       for (g = 0; g < ng; g++)
       {
           s_method_incoh = s_method_incoh/(nframes) ;
           if (bSpectrum == FALSE)
           {
              s_method_nospectrum[g] = s_method_nospectrum[g]/(nframes) ;
              s_method_coh_nospectrum[g] = s_method_coh_nospectrum[g]/(nframes) ;
           }           
           else
           {
              for (qq = 0; qq < nbinq ; qq++)
              {
                  s_method_coh[g][qq] = s_method_coh[g][qq]/(nframes) ;
                  s_method[g][qq] = s_method[g][qq]/(nframes) ;
                  for (i = 0; i< (nbin+1)/2 ; i++)
                  {
                      r = i*binwidth;
                      if ((fade == 0) || (r <= fade ))
                      {
                          s_method_g_r[g][qq] += binwidth*r*sin(arr_q[qq]*r)*(rdf[g][i]-1.0)/arr_q[qq] ;
                      }
                      else
                      {
                          mod_f = sqr(cos((r - fade)*inv_width)) ; 
                          s_method_g_r[g][qq] += mod_f*binwidth*r*sin(arr_q[qq]*r)*(rdf[g][i]-1.0)/arr_q[qq] ;
                      }
                  }               
                  s_method_g_r[g][qq] = s_method_g_r[g][qq]*4.0*M_PI*isize0*invvol + 1.0;
              }
           }
       }
    }
    else if (method[0]=='s')
    {
       for (g = 0; g < ng; g++)
       {
           s_method_incoh = s_method_incoh/(nframes) ;
           if (bSpectrum == FALSE)
           {
              s_method_nospectrum[g] = s_method_nospectrum[g]/(nframes)  ;
              s_method_coh_nospectrum[g] = s_method_coh_nospectrum[g]/(nframes)  ;              
           }
           else
           {
               for (qq = 0; qq < nbinq ; qq++)
               {
                  s_method[g][qq] = s_method[g][qq]/(nframes)  ;
                  s_method_coh[g][qq] = s_method_coh[g][qq]/(nframes)  ;
               }  
           }
       }
    }

    sprintf(gtitle, "Structure factor");
    fp = xvgropen(fnSFACT, gtitle, "q", "S(q)", oenv);
    sprintf(refgt, "%s", "");
    fprintf(fp, "@    s0 legend \"coherent\" \n");
    fprintf(fp, "@target G0.S0\n");
    if (ng == 1)
    {
        if (output_env_get_print_xvgr_codes(oenv))
        {
            fprintf(fp, "@ subtitle \"%s%s - %s\"\n", grpname[0], refgt, grpname[1]);
        }
    }
    else
    {
        if (output_env_get_print_xvgr_codes(oenv))
        {
            fprintf(fp, "@ subtitle \"reference %s%s\"\n", grpname[0], refgt);
        }
        xvgr_legend(fp, ng, (const char**)(grpname+1), oenv);
    }
    for (qq = 0; qq < nbinq  ; qq++)
    {
        if (bSpectrum == TRUE)
        {
            fprintf(fp, "%10g", arr_q[qq]);
        }
        else
        {
            fprintf(fp, "%10g", minq);
        }
        for (g = 0; g < ng; g++)
        {   
            if (bSpectrum == TRUE)
            {
                fprintf(fp, " %10g", s_method_coh[g][qq]);
            }
            else
            {
                fprintf(fp, " %10g", s_method_coh_nospectrum[g]);
            }
        }
        fprintf(fp, "\n");
    }
    fprintf(fp,"&\n");
    fprintf(fp, "@    s1 legend \"incoherent\"\n");
    fprintf(fp, "@target G0.S1\n");
    fprintf(fp, "@type xy\n");
    for (qq = 0; qq < nbinq  ; qq++)
    {
        if (bSpectrum == TRUE) 
        {
           fprintf(fp, "%10g", arr_q[qq]);
        }
        else
        {
          fprintf(fp, "%10g", minq);
        }
        fprintf(fp, " %10g", s_method_incoh);
        fprintf(fp, "\n");
    }
    fprintf(fp,"&\n");
    fprintf(fp, "@    s2 legend \"total\"\n");
    fprintf(fp, "@target G0.S2\n");
    fprintf(fp, "@type xy\n");
    for (qq = 0; qq < nbinq  ; qq++)
    {
        if (bSpectrum == TRUE) 
        {
           fprintf(fp, "%10g", arr_q[qq]);
        }
        else
        {
          fprintf(fp, "%10g", minq);
        }
        for (g = 0; g < ng; g++)
        {
            if (bSpectrum == TRUE)
            {
                fprintf(fp, " %10g", s_method[g][qq]);
            }
            else
            {
                fprintf(fp, " %10g", s_method_nospectrum[g]);
            }
        }
        fprintf(fp, "\n");
    }
    gmx_ffclose(fp);

    do_view(oenv, fnSFACT, NULL);

    if ((fnOSRDF || fnORDF) && method[0]!='s' && method[0]=='m')
    {
        if (fnOSRDF) {fp = xvgropen(fnOSRDF, "S(q) evaluated from g(r)", "q", "S(q)", oenv);}
        if (fnORDF)  {fpn = xvgropen(fnORDF, "Radial distribution function", "r", "g(r)", oenv);}
        if (ng == 1)
        {
            if (output_env_get_print_xvgr_codes(oenv))
            {
                if (fnOSRDF) {fprintf(fp, "@ subtitle \"%s-%s\"\n", grpname[0], grpname[1]);}
                if (fnORDF)  {fprintf(fpn, "@ subtitle \"%s-%s\"\n", grpname[0], grpname[1]);}
            }
        }
        else
        {
            if (output_env_get_print_xvgr_codes(oenv))
            {
                if (fnOSRDF) {fprintf(fp, "@ subtitle \"reference %s\"\n", grpname[0]);}
                if (fnORDF)  {fprintf(fpn, "@ subtitle \"reference %s\"\n", grpname[0]);}
            }
            if (fnOSRDF) {xvgr_legend(fp, ng, (const char**)(grpname+1), oenv);}
            if (fnORDF)  {xvgr_legend(fpn, ng, (const char**)(grpname+1), oenv);}
        }
        if (fnOSRDF)
        {
           for (qq = 0; qq < nbinq  ; qq++)
           {
               fprintf(fp, "%10g", arr_q[qq]);
               for (g = 0; g < ng; g++)
               {
                   fprintf(fp, " %10g", s_method_g_r[g][qq]);
               }
               fprintf(fp, "\n");
           }
           gmx_ffclose(fp);
           do_view(oenv, fnOSRDF, NULL);
        }
        if (fnORDF)
        {
           for (i = 0; (i < nrdf)  ; i++)
           {
               fprintf(fpn, "%10g", i*binwidth);
               for (g = 0; g < ng; g++)
               {
                   fprintf(fpn, " %10g", rdf[g][i]);
               }
               fprintf(fpn, "\n");
           }
           gmx_ffclose(fpn);
           do_view(oenv, fnORDF, NULL);
        }
    }
    if (method[0] == 'c' || method[0] == 'm')
    {
       for (g = 0; g < ng; g++)
       {
          sfree(rdf[g]);
          sfree(s_method[g]);
          sfree(s_method_coh[g]);
          sfree(s_method_g_r[g]);
       }
       sfree(rdf);
       sfree(s_method);
       sfree(s_method_coh);
       sfree(s_method_nospectrum);
       sfree(s_method_coh_nospectrum);
       sfree(analytical_integral);
       sfree(s_method_g_r);
       sfree(arr_q);
       sfree(cos_q);
       sfree(sin_q);
       sfree(temp_method);
    }
    else if (method[0] == 's')  
    {
       for (g = 0; g < ng; g++)
       {
          sfree(s_method[g]);
          sfree(s_method_coh[g]);
       }
       sfree(s_method);
       sfree(s_method_coh);
       sfree(s_method_nospectrum);
       sfree(s_method_coh_nospectrum);
       sfree(analytical_integral);
       sfree(arr_q);
       sfree(temp_method);
    }

    for (i = 0; i < DIM; i++)
    {
        for (j = 0; j < DIM; j++)
        {
            sfree(beta_mol[i][j]);
        }
    }
    for (j = 0; j < DIM; j++)
    {
        sfree(beta_mol[j]);
    }
}

void mol_unitvectors(const rvec xv1, const rvec xv2, const rvec xv3, rvec u1, rvec u2, rvec u3)
{
    rvec_sub( xv2, xv3, u2);
    unitv(u2, u2);
    svmul(2.0, xv1, u1);
    rvec_sub( u1, xv2, u1);
    rvec_sub( u1, xv2, u1);
    unitv(u1, u1);
    cprod(u1 , u2, u3);
    
}


void square_beta(const rvec xv1, const rvec xv2, const rvec xv3, const rvec pout, const rvec pin1, const rvec pin2,  real ***beta_m, real *beta_l, real *beta_sq)
{
    rvec xvec, yvec, zvec;
    real cos200 = 0.0, cos211 = 0.0, cos222 = 0.0, cos020 = 0.0, cos002 = 0.0, cos121 = 0.0, cos112 = 0.0 ;
    rvec_sub( xv2, xv3, xvec); /*this should be x*/
    unitv(xvec, xvec);
    svmul(-2.0, xv1, zvec);
    rvec_add( zvec, xv2, zvec);
    rvec_add( zvec, xv3, zvec);
    unitv(zvec, zvec); /*this should be z*/
    cprod(xvec , zvec, yvec); /*this should be y*/
    cos200 = beta_m[2][0][0]*iprod(zvec, pout)*iprod(xvec, pin1)*iprod(xvec, pin2);
    cos211 = beta_m[2][1][1]*iprod(zvec, pout)*iprod(yvec, pin1)*iprod(yvec, pin2);
    cos222 = beta_m[2][2][2]*iprod(zvec, pout)*iprod(zvec, pin1)*iprod(zvec, pin2);
    cos002 = beta_m[0][0][2]*iprod(xvec, pout)*iprod(xvec, pin1)*iprod(zvec, pin2);
    cos020 = beta_m[0][2][0]*iprod(xvec, pout)*iprod(zvec, pin1)*iprod(xvec, pin2);
    cos112 = beta_m[1][1][2]*iprod(yvec, pout)*iprod(yvec, pin1)*iprod(zvec, pin2);
    cos121 = beta_m[1][2][1]*iprod(yvec, pout)*iprod(zvec, pin1)*iprod(yvec, pin2) ;
    
        

    *beta_sq = cos002*cos002 + 2*cos002*cos020 + cos020*cos020 + 2*cos002*cos112 + 2*cos020*cos112 + cos112*cos112 + 2*cos002*cos121 + 
    2*cos020*cos121 + 2*cos112*cos121 + cos121*cos121 + 2*cos002*cos200 + 2*cos020*cos200 + 2*cos112*cos200 + 2*cos121*cos200 + 
    cos200*cos200 + 2*cos002*cos211 + 2*cos020*cos211 + 2*cos112*cos211 + 2*cos121*cos211 + 2*cos200*cos211 + cos211*cos211 + 
    2*cos002*cos222 + 2*cos020*cos222 + 2*cos112*cos222 + 2*cos121*cos222 + 2*cos200*cos222 + 2*cos211*cos222 + cos222*cos222;

    *beta_l = cos200 + cos211 +cos222 + cos002 + cos020 + cos112 + cos121;
     fprintf(stderr,"beta_l beta_sq %f %f \n",*beta_l, *beta_sq);

}







real rotate_beta(real invnormx, real invnormz, const rvec xv2, const rvec xv3, const rvec pout, const rvec pin1, const rvec pin2, real ***beta_m)
{
    rvec xvec, yvec, zvec;
    //rvec X, Y, Z;
    /*X[0] = 1; X[1] = 0; X[2] = 0 ;
    Y[0] = 0; Y[1] = 1; Y[2] = 0 ;
    Z[0] = 0; Z[1] = 0; Z[2] = 1 ;*/
    real beta_l = 0.0;
    rvec_sub( xv2, xv3, xvec); /*this should be x*/
    svmul(invnormx, xvec ,xvec );
    rvec_add( xv2, xv3, zvec);
    svmul(invnormz, zvec, zvec ); /*this should be z*/
    cprod(xvec , zvec, yvec); /*this should be y*/
    
    /*should be faster than triple loop over i,j,k and uses only the 7 non zero component of water (C2v symmetry) */
    beta_l = beta_m[2][0][0]*iprod(zvec, pout)*iprod(xvec, pin1)*iprod(xvec, pin2) + 
             beta_m[2][1][1]*iprod(zvec, pout)*iprod(yvec, pin1)*iprod(yvec, pin2) +
             beta_m[2][2][2]*iprod(zvec, pout)*iprod(zvec, pin1)*iprod(zvec, pin2) +
             beta_m[0][0][2]*iprod(xvec, pout)*iprod(xvec, pin1)*iprod(zvec, pin2) +
             beta_m[0][2][0]*iprod(xvec, pout)*iprod(zvec, pin1)*iprod(xvec, pin2) +
             beta_m[1][1][2]*iprod(yvec, pout)*iprod(yvec, pin1)*iprod(zvec, pin2) +
             beta_m[1][2][1]*iprod(yvec, pout)*iprod(zvec, pin1)*iprod(yvec, pin2) ;
             /*fprintf(stderr,"cXx %f cYx %f cZx %f\n",iprod(X, xvec), iprod(Y, xvec), iprod(Z,xvec));
             fprintf(stderr,"cXy %f cYy %f cZy %f\n",iprod(X, yvec), iprod(Y, yvec), iprod(Z,yvec));
             fprintf(stderr,"cXz %f cYz %f cZz %f\n",iprod(X, zvec), iprod(Y, zvec), iprod(Z,zvec));*/
   /*clear_rvec(zvec);
   clear_rvec(xvec);
   clear_rvec(yvec);*/

   /* Below is most general (i.e. loop over 27 coordinates of beta 
   for ( i = 0; i < DIM; i++)
   {
       c1 = iprod(um[i], pout);
       for ( k = 0; k < DIM; k++)
       {
           c2[k] = iprod(um[k], pin1);
           for ( q = 0; q < DIM; q++)
           {
               beta_l += c1*c2[k]*iprod(um[q],pin2)*beta_m[i][k][q];
           }
       }
   }*/
   return beta_l;
}

void dipole_atom2molindex(int *n, int *index, t_block *mols)
{
    int nmol, i, j, m;

    nmol = 0;
    i    = 0;
    while (i < *n)
    {
        m = 0;
        while (m < mols->nr && index[i] != mols->index[m])
        {
            m++;
        }
        if (m == mols->nr)
        {
            gmx_fatal(FARGS, "index[%d]=%d does not correspond to the first atom of a molecule", i+1, index[i]+1);
        }
        for (j = mols->index[m]; j < mols->index[m+1]; j++)
        {
            if (i >= *n || index[i] != j)
            {
                gmx_fatal(FARGS, "The index group is not a set of whole molecules");
            }
            i++;
        }
        /* Modify the index in place */
        index[nmol++] = m;
    }
    printf("There are %d molecules in the selection\n", nmol);
    *n = nmol;
}

int gmx_nonlinearopticalscattering(int argc, char *argv[])
{
    const char        *desc[] = {
        "The structure of liquids can be studied by elastic second harmonic scattering (also Hyper-Raileigh scattering).",
        "[THISMODULE] calculates the non-linear optical scattering intensity per molecule in 2 different ways.",
        "The simplest method (sumexp) is to use 1/N<|sum_i beta_IJK(i) exp[iq dot r_i]|^2>.",
        "This however converges slowly with the simulation time.[PAR]",
        "A similar method (modsumexp), but N times more expensive is to do",
        "1/N <sum_i beta_IJK^2> + 1/N <sum_i!=j exp(i q dot r_ij)beta_IJK(i)beta_IJK(j)>.[PAR]"
        "The method cosmo (default) uses the following expression to compute I(q):",
        "I(q)=1/N<sum_i beta_IJK(i)^2> + 1/(2N)< sum_i sum_{i!=j} beta_IJK(i)beta_IJK(j) cos(q dot r_ij) >.",
        "I(q)=incoherent term + coherent term. For more details see Bersohn, et al. JCP 45, 3184 (1966)",
        "The values for the hyperpolarizability for a water molecule beta_IJK are taken by",
        "A. V. Gubskaya et al., Mol. Phys. 99, 13 1107 (2001) (computed at MP4 level with mean field liquid water).",
        "pout, pin1, pin2 are the polarization directions of the three beams.",
        "Common polarization combinations are PSS, PPP, SPP, SSS .",
        "Under Kleinmann symmetry beta_ijj = beta_jij = beta_jji otherwise beta_ijj = beta_jij. [PAR]",
    };
    static gmx_bool    bPBC = TRUE, bNormalize = TRUE, bKleinmannsymm = TRUE, bSpectrum = TRUE;
    static real        binwidth = 0.002, maxq = 100.0, fade = 0.0, faderdf = 0.0;
    static real        kx = 0.0, ky = -1.0, kz = -1.0;
    static int         ngroups = 1, nbinq = 100, pout = 2, pin1 = 1, pin2 = 1;

    static const char *methodt[] = { NULL, "cosmo",  "sumexp", "modsumexp" ,NULL }; 

    t_pargs            pa[] = {
        { "-maxq",      FALSE, etREAL, {&maxq},
        "max wave-vector (1/nm)" },
        { "-nbinq",      FALSE, etINT, {&nbinq},
        "number of bins over wave-vector" },
        { "-qx",         FALSE, etREAL, {&kx}, "direction of q-vector in x , use with sumexp method" },
        { "-qy",         FALSE, etREAL, {&ky}, "direction of q-vector in y , use with sumexp method" },
        { "-qz",         FALSE, etREAL, {&kz}, "direction of q-vector in z , use with sumexp method" },
        { "-pout",         FALSE, etINT, {&pout}, "polarization of outcoming beam (0, 1, or 2). For P choose 2, for S choose 0" },
        { "-pin1",         FALSE, etINT, {&pin1}, "polarization of 1st incoming beam. For P choose 1, for S choose 0 " },
        { "-pin2",         FALSE, etINT, {&pin2}, "polarization of 2nd incoming beam should the same as 1st for second harmonic scattering." },
        { "-bin",      FALSE, etREAL, {&binwidth},
          "Binwidth for g(r) (nm)" },
        { "-method",     FALSE, etENUM, {methodt},
          "I(q) using the different methods" },
        { "-pbc",      FALSE, etBOOL, {&bPBC},
          "Use periodic boundary conditions for computing distances. Without PBC the maximum range will be three times the largest box edge." },
        { "-norm",     FALSE, etBOOL, {&bNormalize}, "Normalize for volume and density" },
        { "-spectrum",    FALSE, etBOOL, {&bSpectrum}, "Compute spectrum between minq and maxq usin nbinq points" },
        { "-klein",    FALSE, etBOOL, {&bKleinmannsymm}, "Assume Kleinmann symmetry" },
        { "-ng",       FALSE, etINT, {&ngroups}, 
          "Number of secondary groups to compute RDFs around a central group" },
        { "-fade",     FALSE, etREAL, {&fade},
          "In the cosmo method the modification function cos^2((rij-fade)*pi/(2*(L/2-fade))) is used in the fourier transform."
          " If fade is 0.0 nothing is done." },
        { "-faderdf",     FALSE, etREAL, {&faderdf},
          "From this distance onwards the RDF is tranformed by g'(r) = 1 + [g(r)-1] exp(-(r/faderdf-1)^2 to make it go to 1 smoothly. "
          " If faderdf is 0.0 nothing is done." },

    };
#define NPA asize(pa)
    const char        *fnTPS, *fnNDX;
    output_env_t       oenv;
    int           *gnx;
    int            nFF[2];
    atom_id      **grpindex;
    char         **grpname = NULL;
    /*gmx_bool       bGkr, bMU, bSlab;*/

    t_filenm           fnm[] = {
        { efTRX, "-f",  NULL,     ffREAD },
        { efTPS, NULL,  NULL,     ffREAD },
        { efNDX, NULL,  NULL,     ffOPTRD },
        { efXVG, "-o",  "non_linear_sfact",    ffWRITE },
        { efXVG, "-osrdf", "sfact_rdf", ffOPTWR },
        { efXVG, "-ordf", "rdf", ffOPTWR },
    };
#define NFILE asize(fnm)
    int            npargs;
    t_pargs       *ppa;
    t_topology    *top;
    int            ePBC;
    int            k, natoms;
    matrix         box;
    
    npargs = asize(pa);
    if (!parse_common_args(&argc, argv, PCA_CAN_VIEW | PCA_CAN_TIME | PCA_BE_NICE,
                           NFILE, fnm, NPA, pa, asize(desc), desc, 0, NULL, &oenv))
    {
        return 0;
    }

    fnTPS = ftp2fn_null(efTPS, NFILE, fnm);
    fnNDX = ftp2fn_null(efNDX, NFILE, fnm);

    if (!fnTPS && !fnNDX)
    {
        gmx_fatal(FARGS, "Neither index file nor topology file specified\n"
                  "Nothing to do!");
    }

    snew(top, ngroups+1);
    ePBC = read_tpx_top(ftp2fn(efTPS, NFILE, fnm), NULL, box,
                        &natoms, NULL, NULL, NULL, top);

    snew(gnx, ngroups+1);
    snew(grpname, ngroups+1);
    snew(grpindex, ngroups+1);
    get_index(&top->atoms, ftp2fn_null(efNDX, NFILE, fnm),
             ngroups +1 , gnx, grpindex, grpname);

    dipole_atom2molindex(&gnx[0], grpindex[0], &(top->mols));
   
    do_nonlinearopticalscattering(top, ftp2fn(efTRX, NFILE, fnm),
           opt2fn("-o", NFILE, fnm), opt2fn_null("-osrdf", NFILE, fnm),
           opt2fn_null("-ordf", NFILE, fnm),
           methodt[0],  bPBC, bNormalize, bKleinmannsymm, bSpectrum ,maxq, nbinq, kx, ky, kz, pout, pin1, pin2 ,binwidth,
           fade, faderdf, gnx, grpindex, grpname, ngroups, oenv);

    return 0;
}

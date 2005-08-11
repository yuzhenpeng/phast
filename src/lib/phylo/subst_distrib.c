/* $Id: subst_distrib.c,v 1.1 2005-08-11 03:54:46 acs Exp $ 
   Written by Adam Siepel, 2005
   Copyright 2005, Adam Siepel, University of California 
*/

/* distributions of numbers of substitutions, prior and posterior */

#include <subst_distrib.h>
#include <misc.h>
#include <sufficient_stats.h>
#include <prob_vector.h>
#include <prob_matrix.h>

/* (used below) compute and return a set of matrices giving p(b, n |
   j), the probability of n substitutions and a final base b given j
   jumps, for all n, j such that 0 <= n,j <= jmax.  If 'condition_on'
   has a nonnegative value, then instead the distribution p(b, n | j,
   a), conditional on a starting base a (whose value is given by
   'condition_on'), will be computed.  The return value A will be such
   that A[b]->data[n][j] = p(b, n | j) [or p(b, n | j, a)] */
Matrix **get_substs_and_bases_given_jumps(JumpProcess *jp, int jmax, 
                                          int condition_on) {
  int i, j, k, n;
  int size = jp->R->nrows;
  Matrix **A = smalloc(size * sizeof(void*));

  for (i = 0; i < size; i++) {
    A[i] = mat_new(jmax, jmax);
    mat_zero(A[i]);
  }

  /* initialization */
  if (condition_on < 0) 
    for (i = 0; i < size; i++)
      A[i]->data[0][0] = jp->mod->backgd_freqs->data[i];
  else 
    A[condition_on]->data[0][0] = 1;

  /* recurrence */
  for (j = 1; j < jmax; j++) {
    for (n = 0; n <= j; n++) {
      for (i = 0; i < size; i++) {
        A[i]->data[n][j] = A[i]->data[n][j-1] * jp->R->data[i][i];
        if (n > 0) 
          for (k = 0; k < size; k++) {
            if (k == i) continue;
            A[i]->data[n][j] += A[k]->data[n-1][j-1] * jp->R->data[k][i];
          }
      }
    }
  }

  return A;
}

/* define jump process based on substitution model */
JumpProcess *sub_define_jump_process(TreeModel *mod, int njumps_max) {
  JumpProcess *jp = smalloc(sizeof(JumpProcess));
  int i, j, size = mod->rate_matrix->size;
  jp->njumps_max = njumps_max;
  jp->R = mat_new(size, size);
  jp->lambda = 0;
  jp->mod = mod;

  /* set lambda to max_a -q_aa */
  for (j = 0; j < size; j++) {
    double val = -mm_get(mod->rate_matrix, j, j);
    if (val > jp->lambda) jp->lambda = val;
  }

  /* now define jump matrix R */
  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      jp->R->data[i][j] = mm_get(mod->rate_matrix, i, j) / jp->lambda;
      if (i == j) jp->R->data[i][j] += 1;
    }
  }

  jp->A = get_substs_and_bases_given_jumps(jp, njumps_max, -1);
  /* A[i]->data[n][j] = p(i, n | j) */

  jp->B = smalloc(size * sizeof(void*));

  for (i = 0; i < size; i++)
    jp->B[i] = get_substs_and_bases_given_jumps(jp, njumps_max, i);
  /* jp->B[i][k]->data[n][j] is p(k, n | j, i), the prob. of n subst. and
     final base k given j jumps and starting base i */

  return jp;
}

void sub_free_jump_process(JumpProcess *jp) {
  int i, j;
  for (i = 0; i < jp->R->nrows; i++)
    mat_free(jp->A[i]);
  free(jp->A);
  for (j = 0; j < jp->R->nrows; j++) {
    for (i = 0; i < jp->R->nrows; i++) 
      mat_free(jp->B[j][i]);
    free(jp->B[j]);
  }
  free(jp->B);
  mat_free(jp->R);
  free(jp);
}

/* compute and return a probability vector giving p(n | t), the probability
   of n substitutions given a branch of length t */
Vector *sub_distrib_branch(JumpProcess *jp, double t) {
  int n, j, i;
  Vector *pois = pv_poisson(jp->lambda * t);  
  Matrix *M = mat_new(pois->size, pois->size);
  Vector *distrib = vec_new(pois->size);

  assert(jp->njumps_max > pois->size);

  /* M defines the distribution of substitutions given jumps:
     M->data[n][j] = p(n | j).  Recall that jp->A[i]->data[n][j] =
     M->p(i, n | j) */

  /* simply marginalize jp->A[i] over i to get M */
  mat_zero(M);
  for (n = 0; n < pois->size; n++) 
    for (j = 0; j < pois->size; j++) 
      for (i = 0; i < jp->R->nrows; i++) 
        M->data[n][j] += jp->A[i]->data[n][j];
  /* i.e., p(n | j) += p(i, n | j) */

  /* now combine with Poisson to get desired distribution */
  vec_zero(distrib);
  for (n = 0; n < pois->size; n++) 
    for (j = 0; j < pois->size; j++) 
      distrib->data[n] += M->data[n][j] * pois->data[j];

  vec_free(pois);

  pv_normalize(distrib);
  return distrib;
}

/* compute and return an array of matrices giving p(b, n | a, t), the
   probability of n substitutions and final base b given starting base
   a and branch length t, for all a, b, and n.  The return value D
   will be such that D[a]->data[b][n] = p(b, n | a, t) */
Matrix **sub_distrib_branch_conditional(JumpProcess *jp, double t) {
  int i, j, n, k;
  Vector *pois = pv_poisson(jp->lambda * t);  
  int size = jp->mod->rate_matrix->size;
  Matrix **D = smalloc(size * sizeof(void*));

  assert(jp->njumps_max > pois->size);

  for (i = 0; i < size; i++) {
    D[i] = mat_new(size, pois->size);
    mat_zero(D[i]);
  }

  /* recall that jp->B[k][i]->data[n][j] is p(i, n | j, k), the
     prob. of n subst. and final base i given j jumps and starting
     base k */

  /* now combine with Poisson to get desired distribution */
  for (k = 0; k < size; k++)
    for (n = 0; n < pois->size; n++) 
      for (j = 0; j < pois->size; j++) 
        for (i = 0; i < size; i++)
          D[k]->data[i][n] += jp->B[k][i]->data[n][j] * pois->data[j];
  /* i.e., p(i, n | k, t) += p(i, n | j, k, t) * p(j | t) */

  vec_free(pois);

  for (i = 0; i < size; i++) 
    pm_normalize(D[i]);

  return D;
}

/* compute and return a probability vector giving the (prior)
   distribution over the number of substitutions for a given tree
   model */
Vector *sub_prior_distrib_site(JumpProcess *jp) {
  return sub_distrib_branch(jp, tr_total_len(jp->mod->tree));
}

/* compute and return a probability vector giving the posterior
   distribution over the number of substitutions per site given a tree
   model and alignment column */
Vector *sub_posterior_distrib_site(JumpProcess *jp, MSA *msa, int tuple_idx) {
  int lidx, n, i, j, k, a, b, c;
  Matrix **L = smalloc(jp->mod->tree->nnodes * sizeof(void*));
  List *traversal = tr_postorder(jp->mod->tree);
  int size = jp->mod->rate_matrix->size;
  Vector *retval;
  int *maxsubst = smalloc(jp->mod->tree->nnodes * sizeof(int));

  assert(jp->mod->order == 0);
  assert(msa->ss != NULL);

  if (jp->mod->msa_seq_idx == NULL)
    tm_build_seq_idx(jp->mod, msa);

  for (lidx = 0; lidx < lst_size(traversal); lidx++) {
    TreeNode *node = lst_get_ptr(traversal, lidx);

    L[node->id] = mat_new(size, 500);
    mat_zero(L[node->id]);
    /* L[node->id]->[a][n] is the joint probability of n substitutions
       and the data beneath node, given that node has label a */

    if (node->lchild == NULL) {    /* leaf -- base case */
      char c = ss_get_char_tuple(msa, tuple_idx, 
                                 jp->mod->msa_seq_idx[node->id], 0);
      if (msa->is_missing[(int)c] || c == GAP_CHAR)
        for (a = 0; a < size; a++)
          L[node->id]->data[a][0] = 1;
      else {
        assert(msa->inv_alphabet[(int)c] >= 0);
        L[node->id]->data[msa->inv_alphabet[(int)c]][0] = 1;
      }

      maxsubst[node->id] = 0;	/* max no. subst. beneath node */
    }
    
    else {            /* internal node -- recursive case */

      Matrix **d_left = sub_distrib_branch_conditional(jp, node->lchild->dparent);
      Matrix **d_right = sub_distrib_branch_conditional(jp, node->rchild->dparent);

      maxsubst[node->id] = max(maxsubst[node->lchild->id] + d_left[0]->ncols - 1, 
                               maxsubst[node->rchild->id] + d_right[0]->ncols - 1);

      for (n = 0; n <= maxsubst[node->id]; n++) {
        for (j = 0; j <= n; j++) {
          int min_i, max_i, min_k, max_k;
          min_i = max(0, j - d_left[0]->ncols + 1);
          max_i = min(j, maxsubst[node->lchild->id]);
          min_k = max(0, n - j - d_right[0]->ncols + 1);
          max_k = min(n - j, maxsubst[node->rchild->id]);

          for (a = 0; a < size; a++) {
            double left = 0, right = 0;
	    
            for (b = 0; b < size; b++) 
              /* i goes from 0 to j, but we can trim off extreme vals */
              for (i = min_i; i <= max_i; i++) 
                left += L[node->lchild->id]->data[b][i] * 
                  d_left[a]->data[b][j-i];

            for (c = 0; c < size; c++) 
              /* k goes from 0 to n-j, but we can trim off extreme vals */
              for (k = min_k; k <= max_k; k++) 
                right += L[node->rchild->id]->data[c][k] * 
                  d_right[a]->data[c][n-j-k];
      
            L[node->id]->data[a][n] += (left * right);
          }
        }
      }

      for (a = 0; a < size; a++) {
        mat_free(d_left[a]);
        mat_free(d_right[a]);
      }
      free(d_left);
      free(d_right);
    }
  }

  retval = vec_new(maxsubst[jp->mod->tree->id] + 1);
  vec_zero(retval);
  for (n = 0; n <= maxsubst[jp->mod->tree->id]; n++)
    for (a = 0; a < size; a++)
      retval->data[n] += L[jp->mod->tree->id]->data[a][n] * 
        jp->mod->backgd_freqs->data[a];

  normalize_probs(retval->data, retval->size);

  /* trim off very small values */
  for (n = maxsubst[jp->mod->tree->id]; n >= 0 && retval->data[n] < 1e-10; n--);
  retval->size = n+1;

  for (lidx = 0; lidx < jp->mod->tree->nnodes; lidx++)
    mat_free(L[lidx]);
  free(L);
  free(maxsubst);

  pv_normalize(retval);
  return retval;
}

/* compute and return a probability vector giving the (prior)
   distribution over the number of substitutions for a given tree
   model and number of sites */
Vector *sub_prior_distrib_alignment(JumpProcess *jp, int nsites) {
  Vector *p = sub_prior_distrib_site(jp);
  Vector *retval = pv_convolve(p, nsites);
  vec_free(p);
  return retval;
}

/* compute and return a probability vector giving the posterior
   distribution over the number of substitutions for a given 
   model and alignment */
Vector *sub_posterior_distrib_alignment(JumpProcess *jp, MSA *msa) {
  int tup;
  Vector *retval;
  Vector **tup_p = smalloc(msa->ss->ntuples * sizeof(void*));
  int *counts = smalloc(msa->ss->ntuples * sizeof(int));

  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    tup_p[tup] = sub_posterior_distrib_site(jp, msa, tup); 
    counts[tup] = msa->ss->counts[tup]; /* have to convert to int */
  }

  retval = pv_convolve_many(tup_p, counts, msa->ss->ntuples);

  for (tup = 0; tup < msa->ss->ntuples; tup++) 
    vec_free(tup_p[tup]);
  free(tup_p);
  free(counts);
  
  return retval;
}

/* compute mean and variance of number of substitutions, given tree
   model and alignment.  These can be obtained without doing the
   convolution */
void sub_posterior_stats_alignment(JumpProcess *jp, MSA *msa, 
                                   double *mean, double *variance) {
  int tup;
  double this_mean, this_var;
  Vector *p;
  *mean = 0; *variance = 0;
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    p = sub_posterior_distrib_site(jp, msa, tup); 
    pv_stats(p, &this_mean, &this_var);
    *mean += this_mean * msa->ss->counts[tup];
    *variance += this_var * msa->ss->counts[tup];
    vec_free(p);
  }
}


/* compute and return a matrix giving the *joint* distribution of the
   number of substitutions in the left and in the right subtree
   beneath the root.  Assumes branch length of 0 to right.  (Reroot
   tree with tr_reroot to get joint for any subtree and supertree.)
   If msa != NULL, the posterior distribution for tuple 'tuple_idx' is
   computed, otherwise the prior distribution is computed.  The
   returned matrix is such that retval->data[n1][n2] is the posterior
   probability of n1 substitutions in the left subtree and n2
   substitutions in the right subtree  */
Matrix *sub_joint_distrib_site(JumpProcess *jp, MSA *msa, int tuple_idx) {
  int lidx, n, i, j, k, a, b, c, n1, n2, n1_max = 0, n2_max = 0, done;
  Matrix **L = smalloc(jp->mod->tree->nnodes * sizeof(void*));
  List *traversal = tr_postorder(jp->mod->tree);
  int size = jp->mod->rate_matrix->size;
  Matrix *retval;
  Matrix **d_left, **d_right;
  double sum;
  int *maxsubst = smalloc(jp->mod->tree->nnodes * sizeof(int));

  assert(jp->mod->order == 0);

  if (msa != NULL && jp->mod->msa_seq_idx == NULL)
    tm_build_seq_idx(jp->mod, msa);

  for (lidx = 0; lidx < lst_size(traversal); lidx++) {
    TreeNode *node = lst_get_ptr(traversal, lidx);

    L[node->id] = mat_new(size, 500);
    mat_zero(L[node->id]);
    /* L[node->id]->[a][n] is the joint probability of n substitutions
       and the data beneath node, given that node has label a */

    if (node->lchild == NULL) {    /* leaf -- base case */
      char c;
      if (msa != NULL)
        c = ss_get_char_tuple(msa, tuple_idx, jp->mod->msa_seq_idx[node->id], 0);
      if (msa == NULL || msa->is_missing[(int)c] || c == GAP_CHAR)
        for (a = 0; a < size; a++)
          L[node->id]->data[a][0] = 1;
      else {
        assert(msa->inv_alphabet[(int)c] >= 0);
        L[node->id]->data[msa->inv_alphabet[(int)c]][0] = 1;
      }

      maxsubst[node->id] = 0;	/* max no. subst. beneath node */
    }
    
    else {            /* internal node -- recursive case */

      d_left = sub_distrib_branch_conditional(jp, node->lchild->dparent);
      d_right = sub_distrib_branch_conditional(jp, node->rchild->dparent);

      maxsubst[node->id] = max(maxsubst[node->lchild->id] + d_left[0]->ncols - 1, 
                               maxsubst[node->rchild->id] + d_right[0]->ncols - 1);
      
      if (node == jp->mod->tree) {
        /* save these for below */
        n1_max = maxsubst[jp->mod->tree->lchild->id] + d_left[0]->ncols;
        n2_max = maxsubst[jp->mod->tree->rchild->id] + d_right[0]->ncols;
      }

      for (n = 0; n <= maxsubst[node->id]; n++) {
        for (j = 0; j <= n; j++) {	
          int min_i, max_i, min_k, max_k;
          min_i = max(0, j - d_left[0]->ncols + 1);
          max_i = min(j, maxsubst[node->lchild->id]);
          min_k = max(0, n - j - d_right[0]->ncols + 1);
          max_k = min(n - j, maxsubst[node->rchild->id]);

          for (a = 0; a < size; a++) {
            double left = 0, right = 0;

            for (b = 0; b < size; b++) 
              for (i = min_i; i <= max_i; i++) 
                left += L[node->lchild->id]->data[b][i] * 
                  d_left[a]->data[b][j-i];

            for (c = 0; c < size; c++) 
              for (k = min_k; k <= max_k; k++) 
                right += L[node->rchild->id]->data[c][k] * 
                  d_right[a]->data[c][n-j-k];
      
            L[node->id]->data[a][n] += (left * right);
          }
        }
      }

      for (a = 0; a < size; a++) {
        mat_free(d_left[a]);
        mat_free(d_right[a]);
      }
      free(d_left);
      free(d_right);
    }
  }

  retval = mat_new(n1_max, n2_max);
  mat_zero(retval);
  d_left = sub_distrib_branch_conditional(jp, jp->mod->tree->lchild->dparent);
  sum = 0;
  for (n1 = 0; n1 < n1_max; n1++) {
    for (n2 = 0; n2 < n2_max; n2++) {
      for (a = 0; a < size; a++) {
        double left = 0;
        int min_i = max(0, n1 - d_left[a]->ncols + 1);
        for (b = 0; b < size; b++) 
          for (i = min_i; i <= n1; i++) 
            left += L[jp->mod->tree->lchild->id]->data[b][i] * 
              d_left[a]->data[b][n1-i];
        retval->data[n1][n2] += left * jp->mod->backgd_freqs->data[a] * 
          L[jp->mod->tree->rchild->id]->data[a][n2];
      }
      sum += retval->data[n1][n2];
    }
  }
  mat_scale(retval, 1/sum);     /* normalize */

  /* trim off very small values */
  done = FALSE;
  for (n1 = n1_max-1; !done && n1 >= 0; n1--) 
    for (n2 = 0; !done && n2 < n2_max; n2++) 
      if (retval->data[n1][n2] >= 1e-10) {
        n1_max = n1+1;
        done = TRUE;
      }
  done = FALSE;
  for (n2 = n2_max-1; !done && n2 >= 0; n2--) 
    for (n1 = 0; !done && n1 < n1_max; n1++) 
      if (retval->data[n1][n2] >= 1e-10) {
        n2_max = n2+1;
        done = TRUE;
      }
  mat_resize(retval, n1_max, n2_max);

  for (a = 0; a < size; a++) 
    mat_free(d_left[a]);
  free(d_left);

  for (lidx = 0; lidx < jp->mod->tree->nnodes; lidx++)
    mat_free(L[lidx]);
  free(L);

  pm_normalize(retval);
  return retval;
}

/* compute and return a probability matrix giving the (prior) joint
   distribution over the number of substitutions in the left and in
   the right subtree beneath the root, assuming the given tree model
   and number of sites.  As above, designed for use with tr_reroot.
   The returned matrix is such that retval->data[x][y] is the
   posterior probability of x substitutions in the left subtree and y
   substitutions in the right subtree */
Matrix *sub_prior_joint_distrib_alignment(JumpProcess *jp, int nsites) {
  Matrix *p = sub_joint_distrib_site(jp, NULL, -1);
  Matrix *retval = pm_convolve(p, nsites);
  mat_free(p);
  return retval;
}

/* compute and return a probability matrix giving the posterior joint
   distribution over the number of substitutions in the left and in
   the right subtree beneath the root, given a tree model and an
   alignment.  As above, designed for use with tr_reroot.  The
   returned matrix is such that retval->data[x][y] is the posterior
   probability of x substitutions in the left subtree and y
   substitutions in the right subtree */
Matrix *sub_posterior_joint_distrib_alignment(JumpProcess *jp, MSA *msa) {
  int tup;
  Matrix *retval;
  Matrix **tup_p = smalloc(msa->ss->ntuples * sizeof(void*));
  int *counts = smalloc(msa->ss->ntuples * sizeof(int));
  
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    tup_p[tup] = sub_joint_distrib_site(jp, msa, tup); 
    counts[tup] = msa->ss->counts[tup]; /* have to convert to int */
  }

  retval = pm_convolve_many(tup_p, counts, msa->ss->ntuples);

  for (tup = 0; tup < msa->ss->ntuples; tup++) 
    mat_free(tup_p[tup]);
  free(tup_p);
  free(counts);

  return retval;
}

/* compute mean and (marginal) variance of number of substitutions in
   left and right subtree, given tree model and alignment.  These can
   be obtained without doing the convolution */
void sub_posterior_joint_stats_alignment(JumpProcess *jp, MSA *msa, 
                                         double *mean_tot, double *var_tot,
                                         double *mean_left, double *var_left,
                                         double *mean_right, double *var_right) {
  int tup;
  Matrix *p;
  double this_mean, this_var;
  Vector *marg_x, *marg_y, *marg_tot;
  *mean_left = *var_left = *mean_right = *var_right = *mean_tot = *var_tot = 0;
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    p = sub_joint_distrib_site(jp, msa, tup); 
    marg_x = pm_marg_x(p);
    pv_stats(marg_x, &this_mean, &this_var);
    *mean_left += this_mean * msa->ss->counts[tup];
    *var_left += this_var * msa->ss->counts[tup];
    marg_y = pm_marg_y(p);
    pv_stats(marg_y, &this_mean, &this_var);
    *mean_right += this_mean * msa->ss->counts[tup];
    *var_right += this_var * msa->ss->counts[tup];
    marg_tot = pm_marg_tot(p);
    pv_stats(marg_tot, &this_mean, &this_var);
    *mean_tot += this_mean * msa->ss->counts[tup];
    *var_tot += this_var * msa->ss->counts[tup];
    vec_free(marg_x);
    vec_free(marg_y);
    vec_free(marg_tot);
    mat_free(p);
  }
}

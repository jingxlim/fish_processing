#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lapacke.h"

/*******************************************************************************
 *                                   Globals                                   *
 *******************************************************************************/

/* Macro Function Definitions */
#define  max(x,y)       ((x)>(y)?(x):(y))
#define  min(x,y)       ((x)<(y)?(x):(y))

/* Global Vars */
double *vio_fitness; // made available for sorting use in cmp

/*******************************************************************************
 *                            Function Declaration                             *
 *******************************************************************************/

/* y = D*x */
void   Dx(const int n,
	  const double *x,
	  double *y); 

/* y = D'*x */
void   DTx(const int n,
	   const double *x,
	   double *y);

/* x = y - lambda*D'*z */
void   update_primal(int n,
		     double *x,
		     const double *y,
		     const double *z,
		     double lambda);

/* z_a : D_a*D_a'*z_a == D_a * ( (y / lambda) - D_i' z_i)
 * potential optimization opportunity:
 * precomp D_a*(y/lambda) and use trick for D_a*D_i'*z_i
 */
int   update_dual(const int n,
		  const double *y,
		  double *z,
		  const double lambda,
		  double *div_zi,
		  double *ab,
		  double *b);

int locate_violators(const int n,
		     const double *z,
		     const double lambda,
		     const double *diff_x,
		     int *vio_index,
		     double *vio_fitness,
		     int *vio_sort);

void reassign_violators(const int n_vio,
			double *z,
			const int *vio_index,
			const int *vio_sort);

/* print first n array double in array to stdout */
void print_dvec(const int n,
		const double *x);

/* comparator to sort violators by fitness */
int cmp(const void *a,
	const void *b);

/*******************************************************************************
 *                                 Main Solver                                 *
 *******************************************************************************/

int active_set(const int n,           // data length
	       const double *y,       // target
	       const double lambda,   // regularization parameter
	       double *x,             // primal variable
	       double *z,             // dual variable
	       double p,              // proportion of violators to reassign
	       const int m,           // size of violator hostory queue
	       const double delta_s,  // proportion by which p is shrunk
	       const double delta_e,  // proportion by which p is grown
	       const int maxiter,     // max num outer loop iterations
	       const int verbose)
{
    /************************** Initialize Variables ***************************/
    double *diff_x;
    double *div_zi;
    int *vio_index;
    int *vio_queue;
    int *vio_sort;    
    int queue_index;
    int min_queue;
    int min_queue_index;    
    int max_queue;
    int max_queue_index;
    double *ab;
    double *b;
    int n_vio;
    int n_active;
    
    /***************************** Allocate Memory *****************************/
    diff_x = malloc(sizeof(double)*(n-2));
    div_zi = malloc(sizeof(double)*n);
    vio_fitness = malloc(sizeof(double)*(n-2));
    vio_index = malloc(sizeof(int)*(n-2));
    vio_sort = malloc(sizeof(int)*(n-2));    
    vio_queue = malloc(sizeof(int)*m);
    ab = malloc(sizeof(double)*n*3);
    b = malloc(sizeof(double)*n);
    /************************ Prepare Queue Variables **************************/
    queue_index = 0;
    vio_queue[0] = n;
    min_queue = n;
    min_queue_index = 0;    
    for (int i = 1; i < m; i++){
      vio_queue[i] = n;
    }
    max_queue = n;
    max_queue_index = m - 1;
    
    /* prepare to begin optimization */
    if (verbose) {    
	fprintf(stderr,"____________________________\n");            
	fprintf(stderr,"%s%s%s%s\n", "|Iter|", "Violators|", "Active|", "Prop|");
    }
    /************************** Opt Routine Main Loop **************************/
    for(int iter = 1; iter <= maxiter; iter++) {

      /************************ Subspace Minimization **************************/
      n_active = update_dual(n, y, z, lambda, div_zi, ab, b);
      update_primal(n, x, y, z, lambda);
      Dx(n, x, diff_x);
      
      /*************************** Update Partition ****************************/

      // Count, evaluate (fitness), and store violators
      n_vio = locate_violators(n, z, lambda, diff_x, vio_index, vio_fitness, vio_sort);
      
      // Update safeguard queue and proportion of violators to be reassigned
      if(n_vio < min_queue){
	// inflate proprtion
	p = min(delta_e * p , 1);
	// push new min into queue
	vio_queue[queue_index] = n_vio;
	min_queue = n_vio;
	min_queue_index = queue_index;
	// if max val in queue was replaced, compute new max
	if (queue_index == max_queue_index){
	  max_queue = 0;
	  for (int j = 0; j < m; j++) {
	    if (vio_queue[j] > max_queue) {
	      max_queue = vio_queue[j];
	      max_queue_index = j;
	    }
	  }	  	  
	}
	// increment queue index	
	queue_index = (queue_index + 1) % m;
      } else if(n_vio >= max_queue){
	// deflate proprtion	
	p = max(delta_s * p, 1 / n_vio);
      } else{
	// push new value into queue
	vio_queue[queue_index] = n_vio;
	// if max/min val in queue was replaced, compute new max/min
	if (queue_index == max_queue_index){
	  max_queue = 0;
	  for (int j = 0; j < m; j++) {
	    if (vio_queue[j] > max_queue) {
	      max_queue = vio_queue[j];
	      max_queue_index = j;
	    }
	  }	  
	} else if (queue_index == min_queue_index){
	  min_queue = n;
	  for (int j = 0; j < m; j++) {
	    if (vio_queue[j] < min_queue) {
	      min_queue = vio_queue[j];
	      min_queue_index = j;
	    }
	  }
	}	        
	// increment queue index
	queue_index = (queue_index + 1) % m;	
      }

      // Print Status
      if (verbose) {
	fprintf(stderr,"|%4d|%9d|%6d|%4.2f|\n", iter, n_vio, n_active, p);      
      }

      // Check Termination Criterion=
      if(n_vio == 0) {
	/*********************** Algorithm Converged ***************************/

	// Print Status
	if (verbose) {
	  fprintf(stderr,"Solved\n");    	
	}

	// Free Allocated Memory
	free(diff_x);
	free(div_zi);
	free(ab);
	free(b);
	free(vio_fitness);
	free(vio_index);
	free(vio_sort);    
	free(vio_queue);

	// Return Success Code
	return(0);
      }

      // Sort violator indices by fitness value
      qsort(vio_sort, n_vio, sizeof(int), cmp);

      // Reassign first p * n_vio violators
      n_vio = max((int)round(p * (double)n_vio), 1);
      reassign_violators(n_vio, z, vio_index, vio_sort);
    }
    
    /******************** Algorithm Failed To Converge *************************/

    // Print Status
    if (verbose) {    
      fprintf(stderr,"MAXITER Exceeded.\n");
    }

    // Free Allocated Memory
    free(diff_x);
    free(div_zi);
    free(ab);
    free(b);
    free(vio_fitness);
    free(vio_index);
    free(vio_sort);    
    free(vio_queue);

    // Return Failure Code
    return(-1);
}

/******************************************************************************
 *                                Subproblems                                 *
 ******************************************************************************/

/* Given the dual variable, updates the primal according to 
        x = y - lamba*dT*z 
   Computation is performed in place and within a single O(n)
   loop that computes div_z_i while updating x_i
*/
void update_primal(const int n,
		   double *x,
		   const double *y,
		   const double *z,
		   const double lambda)
{
  int i;
  // x[0] = y[0] + z[0] * lambda
  *x++ = *y + (*z * lambda); y++;
  // x[1] = y[1] - (z[1] - 2*z[0])* lambda  
  *x++ = *y + ((*(z+1) - *z - *z) * lambda); y++;
  for (i = 2; i < n-2; i++, y++, z++){ // i = 2,...,n-3
    // x[i] = y[i] + (z[i-2] + z[i] - 2*z[i-1]) * lambda        
    *x++ = *y + ((*z - *(z+1) - *(z+1) + *(z+2)) * lambda);
  }
  // x[n-2] = y[n-2] + (z[n-4] - 2*z[n-3]) * lambda  
  *x++ = *y + ((*z - *(z+1) - *(z+1)) * lambda); y++; z++;
  // x[n-1] = y[n-1] + z[n-3] * lambda
  *x = *y + (*z * lambda);
}

/* Given a partition dual, update the active set according to
        D[A]TD[A] z[A] = D[A](y - lamba*D[I]T*z[I]) / lambda 
   Quindiagonal matrix solved with LAPACKE's dpbsv interface
*/
int update_dual(const int n,
		const double *y,
		double *z,
		const double lambda,
		double *div_zi,
		double *ab,
		double *b)
{
  /* Initiliaze Counters */
  int i;
  int ik;
  int previous = -3;
  int two_previous = -3;  
  int k = n - 2; // start with all dual coordinates active
  
  /* Compute div_zi = D[I}^T z[I] and count active coordinates */
  div_zi[0] = 0;
  div_zi[1] = 0; 
  for(i = 0; i < n - 2; i++) {
    div_zi[i+2] = 0;
    if(z[i] == 1 || z[i] == -1) {
      k--; // Remove inactive coordinate from active count
      div_zi[i] -= z[i];
      div_zi[i+1] += 2*z[i];
      div_zi[i+2] -= z[i];
    }
  }

  /* compute diagonals of D[A]D[A]^T and targets = D[A] resid */
  ik = 0;
  for (i = 0; i < n - 2 ; i++) {
    if (z[i] != 1 && z[i] != -1){
      
      /* Update Diag Matrix Content */
      
      // All main diag inner products eval to 6.0
      ab[ik+2*k] = 6.0;
      
      // off diag: If previous element in A is off by 1 or 2, inner product evals to -4 or 1 resp
      if (i-previous == 1){
	ab[ik+k] =-4.0;
      } else if (i-previous == 2){
	ab[ik+k] = 1.0;
      } else {
	ab[ik+k] = 0.0;
      }
      
      // 2nd off diag: If element in A is off by two, inner product evals to 1
      if (i-two_previous == 2){
	ab[ik] = 1.0;
      } else {
	ab[ik] = 0.0;
      }
      
      // Update Counters For Previous Time Element Was In A
      two_previous = previous;
      previous = i;

      /* Update target content */      
      b[ik] = ((y[i+1] + y[i+1] - y[i] - y[i+2])/lambda) - div_zi[i+1] - div_zi[i+1] + div_zi[i] + div_zi[i+2];

      /* Update active set counter */
      ik++;
    }
  }
  
  // compute matrix solve
  int result = LAPACKE_dpbsv(LAPACK_ROW_MAJOR, 'U', k, 2, 1, ab, k, b, 1);
  if(result != 0) fprintf(stderr,"LAPACKE: %d\n", result);

  // update zA
  ik = 0;
  for (i = 0; i < n - 2; i++) {
    if (z[i] != 1 && z[i] != -1){
      z[i] = b[ik];
      ik++;
    } 
  }
  
  return k;
}

/* locate, count and eval fitness of violators */
int locate_violators(const int n,
		     const double *z,
		     const double lambda,		     
		     const double *diff_x,
		     int *vio_index,
		     double *vio_fitness,
		     int *vio_sort)
{
  int n_vio = 0;  // number of violations located
  for(int i = 0; i < n - 2; i++) {
    if(z[i] == 1) {
      if(diff_x[i] < 0) {
	vio_index[n_vio] = i;
	vio_fitness[n_vio] = max(lambda * fabs(diff_x[i]), 1);
	vio_sort[n_vio] = n_vio;
	n_vio++;
      }
    } else if(z[i] == -1) {
      if(diff_x[i] > 0) {
	vio_index[n_vio] = i;	
	vio_fitness[n_vio] = max(lambda * fabs(diff_x[i]), 1);
	vio_sort[n_vio] = n_vio;	
	n_vio++;
      }
    } else {
      if(z[i] > 1) {
	vio_index[n_vio] = i;	
	vio_fitness[n_vio] = max(lambda * fabs(diff_x[i]), fabs(z[i]));
	vio_sort[n_vio] = n_vio;	
	n_vio++;
      } else if(z[i] < -1) {
	vio_index[n_vio] = i;	
	vio_fitness[n_vio] = max(lambda * fabs(diff_x[i]), fabs(z[i]));
	vio_sort[n_vio] = n_vio;	
	n_vio++;
      }
    }
  }
  return n_vio;
}
/* locate, count and eval fitness of violators */
void reassign_violators(const int n_vio,
			double *z,
			const int *vio_index,
			const int *vio_sort)
{
  for(int i = 0; i < n_vio; i++) {
    if(z[vio_index[vio_sort[i]]] == 1) {
      z[vio_index[vio_sort[i]]] = 0;
    } else if(z[vio_index[vio_sort[i]]] == -1) {
      z[vio_index[vio_sort[i]]] = 0;
    } else {
      if(z[vio_index[vio_sort[i]]] > 1) {
	z[vio_index[vio_sort[i]]] = 1;
      } else if(z[vio_index[vio_sort[i]]] < -1) {
	z[vio_index[vio_sort[i]]] = -1;
      }
    }
  }
}

/******************************************************************************
 *                             Matrix Operators                               *
 ******************************************************************************/

/* Computes y = D*x, where x has length n
 *
 *     | -1  2  -1  0  0 |
 * y = | 0  -1  2  -1  0 |*x
 *     | 0  0  -1  2  -1 |
 */
void Dx(const int n, const double *x, double *y)
{
    int i;
    for (i = 0; i < n-2; i++, x++)
        *y++ = -*x+*(x+1)+*(x+1)-*(x+2); /* y[0..n-3]*/
}

/* Computes y = D^T*x, where x has length n
 *
 *     | -1   0   0 |
 *     |  2  -1   0 |
 * y = | -1   2  -1 |*x
 *     |  0  -1   2 |
 *     |  0   0  -1 |
 */
void DTx(const int n, const double *x, double *y)
{
    int i;
    *y++ = -*x;                          /* y[0]     */
    *y++ = *x+*x-*(x+1);                 /* y[1]     */
    for (i = 2; i < n; i++,x++)
        *y++ = -*x+*(x+1)+*(x+1)-*(x+2); /* y[2..n-1]*/
    *y++ = -*x+*(x+1)+*(x+1); x++;       /* y[n]     */
    *y = -*x;                            /* y[n+1]   */
}

/******************************************************************************
 *                            Utility Functions                               *
 ******************************************************************************/

/* Comparator used in qsort to sort violator indices 
 * in descending order of violator fitness. vio_fitness
 * has been made available as global variable.
 */
int cmp(const void *a, const void *b){
  int ia = *(int *)a;
  int ib = *(int *)b;
  return vio_fitness[ia] > vio_fitness[ib] ? -1 : vio_fitness[ia] < vio_fitness[ib];
}

/* Print vector of double type */
void print_dvec(const int n, const double *x)
{
    int i;
    fprintf(stdout,"\n");
    for (i = 0; i < n; i++)
        fprintf(stdout,"%e\n",x[i]);
    fprintf(stdout,"\n");
}

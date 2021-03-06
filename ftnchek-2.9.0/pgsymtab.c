/* pgsymtab.c:

		Routines associated with printing of global symbol table info

    Copyright (C) 1993 by Robert K. Moniot.
    This program is free software.  Permission is granted to
    modify it and/or redistribute it, retaining this notice.
    No guarantees accompany this software.

	Shared functions defined:

		arg_array_cmp()   Compares subprogram calls with defns.
		check_arglists()  Scans global symbol table for subprograms
				  and finds subprogram defn if it exists.
		check_comlists()  Scans global symbol table for common blocks.
		check_com_usage() Checks usage status of common blocks & vars


	Private functions defined:
		arg_array_cmp()	  Compares arg lists of subprog calls/defns
		com_cmp_lax()	  Compares common blocks at strictness 1,2
		com_cmp_strict()  Compares common blocks at strictness 3
		com_element_usage() Checks set/used status of common variables
		com_block_usage() Checks for dead common blocks & variables
		print_modules()	  Prints names from a list of gsymt pointers.
		sort_gsymbols()	  Sorts the list of gsymt names.
		swap_gsymptrs()	  Swaps a pair of pointers.
		visit_child()	  Recursively visits callees of module,
				  printing call tree as it goes.
		visit_child_reflist() Recursively visits callees of module,
				  printing reference list as it goes.
		print_crossrefs() Prints list of callers of module.
		toposort()	  Topological sort of call tree.
		sort_child_list() Sorts linked list of callees.
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "ftnchek.h"
#define PGSYMTAB
#include "symtab.h"



PROTO(PRIVATE void arg_array_cmp,( char *name, ArgListHeader *args1,
			   ArgListHeader *args2 ));
PROTO(PRIVATE void arg_error_locate,( ArgListHeader *alh ));
PROTO(PRIVATE int block_is_volatile,( ComListHeader *clist, Gsymtab *main_module ));
PROTO(PRIVATE void check_nameclash,(void));
PROTO(PRIVATE int cmp_error_head,(char *name, char *message));
PROTO(PRIVATE void com_block_usage,( char *name, ComListHeader *cl1 ));
PROTO(PRIVATE void com_cmp_lax,( char *name, ComListHeader *c1, ComListHeader *c2 ));
PROTO(PRIVATE void com_cmp_strict,( char *name, ComListHeader *c1,
			    ComListHeader *c2 ));
PROTO(PRIVATE ComListHeader * com_declared_by,( Gsymtab *comblock, Gsymtab *module ));
PROTO(PRIVATE void com_element_usage,( char *name, ComListHeader *r_cl,
			       ComListElement *r_list, int r_num ));
PROTO(PRIVATE void com_error_locate,( ComListHeader *clh ));
PROTO(PRIVATE ComListHeader * com_tree_check,( Gsymtab *comblock, Gsymtab
				       *module, int level ));
#ifdef DEBUG_COM_USAGE
PROTO(PRIVATE void print_comvar_usage,( ComListHeader *comlist ));
#endif
PROTO(PRIVATE void print_crossrefs,( void ));
PROTO(PRIVATE void print_cycle_nodes,( Gsymtab gsymt[], int nsym, Gsymtab
			       *node_list[], int node_count, int
			       parent_count[] ));
PROTO(PRIVATE void print_modules,( unsigned n, Gsymtab *list[] ));
PROTO(PRIVATE ChildList * sort_child_list,( ChildList *child_list ));
PROTO(PRIVATE void sort_gsymbols ,( Gsymtab *glist[], int n ));
PROTO(PRIVATE void swap_gsymptrs ,( Gsymtab **x_ptr, Gsymtab **y_ptr));
PROTO(PRIVATE int toposort,( Gsymtab gsymt[], int nsym ));
PROTO(PRIVATE void visit_child,( Gsymtab *gsymt, int level ));
PROTO(PRIVATE void visit_child_reflist,( Gsymtab *gsymt ));
#ifdef VCG_SUPPORT
PROTO(PRIVATE void visit_child_vcg,( Gsymtab *gsymt, int level ));
#endif



		/* Macro for testing whether an arglist or comlist header is
		   irrelevant for purposes of error checking: i.e. it comes
		   from an unvisited library module. */
#define irrelevant(list) ((list)->module->library_module &&\
				!(list)->module->visited_somewhere)

#define pluralize(n) ((n)==1? "":"s")	/* singular/plural suffix for n */

#define CMP_ERR_LIMIT 3	/* stop printing errors after this many */


PRIVATE int cmp_error_count;
PRIVATE int
#if HAVE_STDC
cmp_error_head(char *name, char *message)
#else /* K&R style */
cmp_error_head(name,message)
     char *name,*message;
#endif /* HAVE_STDC */
	/* Increment error count, and if it is 1, print header for arg
	   mismatch error messages.  If it is past limit, print "etc"
	   and return TRUE, otherwise return FALSE.
	   */
{
		/* stop after limit: probably a cascade */
	if(++cmp_error_count > CMP_ERR_LIMIT) {
	  (void)fprintf(list_fd,"\n etc...");
	  return TRUE;
	}
	if(cmp_error_count == 1)
	  (void)fprintf(list_fd,"\nSubprogram %s: %s",name,message);
	return FALSE;
}
PRIVATE void
#if HAVE_STDC
arg_error_locate(ArgListHeader *alh)	/* Gives module, line, filename for error messages */
#else /* K&R style */
arg_error_locate(alh)	/* Gives module, line, filename for error messages */
     ArgListHeader *alh;
#endif /* HAVE_STDC */
{
  if(novice_help) {		/* oldstyle messages */
    (void)fprintf(list_fd," in module %s line %u file %s",
		    alh->module->name,
		    alh->line_num,
		    alh->filename);
    if(alh->filename != alh->topfile) /* Track include filename */
      (void)fprintf(list_fd," (included at line %u in %s)",
		    alh->top_line_num,
		    alh->topfile);
  }
  else {			/* lint-style messages */
    (void)fprintf(list_fd," in module %s of \"%s\", line %u",
		    alh->module->name,
		    alh->filename,
		    alh->line_num);
    if(alh->filename != alh->topfile) /* Track include filename */
      (void)fprintf(list_fd," (\"%s\", line %u)",
		    alh->topfile,
		    alh->top_line_num);
  }
}

PRIVATE void
#if HAVE_STDC
com_error_locate(ComListHeader *clh)	/* Gives module, line, filename for error messages */
#else /* K&R style */
com_error_locate(clh)	/* Gives module, line, filename for error messages */
     ComListHeader *clh;
#endif /* HAVE_STDC */
{
  if(novice_help) {		/* oldstyle messages */
    (void)fprintf(list_fd," in module %s line %u file %s",
		    clh->module->name,
		    clh->line_num,
		    clh->filename);
    if(clh->filename != clh->topfile) /* Track include filename */
      (void)fprintf(list_fd," (included at line %u in %s)",
		    clh->top_line_num,
		    clh->topfile);
  }
  else	{			/* lint-style messages */
    (void)fprintf(list_fd," in module %s of \"%s\", line %u",
		    clh->module->name,
		    clh->filename,
		    clh->line_num);
    if(clh->filename != clh->topfile) /* Track include filename */
      (void)fprintf(list_fd," (\"%s\", line %u)",
		    clh->topfile,
		    clh->top_line_num);
  }
}

PRIVATE void
#if HAVE_STDC
arg_array_cmp(char *name, ArgListHeader *args1, ArgListHeader *args2)
#else /* K&R style */
arg_array_cmp(name,args1,args2)
#endif /* HAVE_STDC */
     		/* Compares subprogram calls with definition */
#if HAVE_STDC
#else /* K&R style */
	char *name;
	ArgListHeader *args1, *args2;
#endif /* HAVE_STDC */
{
	int i;
	int  n,
	     n1 = args1->numargs,
	     n2 = args2->numargs;
	ArgListElement *a1 = args1->arg_array,
		       *a2 = args2->arg_array;

	n = (n1 > n2) ? n2: n1;		/* n = min(n1,n2) */

	if (check_args_number && n1 != n2){
	  cmp_error_count = 0;
	  (void) cmp_error_head(name,"varying number of arguments:");

	  (void)fprintf(list_fd,"\n    %s with %d argument%s",
		    args1->is_defn? "Defined":"Invoked",
	    	    n1,pluralize(n1));
	  arg_error_locate(args1);

	  (void)fprintf(list_fd,"\n    %s with %d argument%s",
		    args2->is_defn? "Defined":"Invoked",
		    n2,pluralize(n2));
	  arg_error_locate(args2);
	}

	if(check_args_type)
	{	/* Look for type mismatches */
	    cmp_error_count = 0;
	    for (i=0; i<n; i++) {
	      int c1 = storage_class_of(a1[i].type),
	          c2 = storage_class_of(a2[i].type),
		  t1 = datatype_of(a1[i].type),
	          t2 = datatype_of(a2[i].type),
		  s1 = a1[i].size,
		  s2 = a2[i].size,
		  defsize1 = (s1==size_DEFAULT),
		  defsize2 = (s2==size_DEFAULT);
				/* cmptype is type to use for mismatch test.
				   Basically cmptype=type but DP matches
				   REAL, DCPX matches CPLX, and hollerith
				   matches any numeric or logical type
				   but not  character.  The single/double
				   match will be deferred to size check. */
	      int cmptype1= (t1==type_HOLLERITH && t2!=type_STRING)?
				t2:type_category[t1];
	      int cmptype2= (t2==type_HOLLERITH && t1!=type_STRING)?
				t1:type_category[t2];

		/* If -portability, do not translate default sizes so
		   they will never match explicit sizes. */
	      if(!(port_mixed_size || local_wordsize==0)) {
		if(defsize1)
		  s1 = type_size[t1];
		if(defsize2)
		  s2 = type_size[t2];
	      }

	      if(s1 < 0 || s2 < 0) { /* char size_ADJUSTABLE or UNKNOWN */
		s1 = s2 = size_DEFAULT;	/* suppress warnings on size */
		defsize1 = defsize2 = TRUE;
	      }

			 /* Require exact match between storage classes and
			    compatible data type.  If that is OK, then for
			    non-char args require exact size match.  For char
			    and hollerith defer size check to other section.
			  */
	    if( (c1 != c2) || (cmptype1 != cmptype2) || ( (s1 != s2) &&
			is_num_log_type(t1) && is_num_log_type(t2) ) ) {

		if(cmp_error_head(name," argument data type mismatch"))
		  break;

		(void)fprintf(list_fd, "\n  at position %d:", i+1);
#ifdef KEEP_ARG_NAMES
		(void)fprintf(list_fd,"\n    %s arg %s is type %s",
			    args1->is_defn? "Dummy": "Actual",
			    a1[i].name,
			    type_name[t1]);
#else
		(void)fprintf(list_fd,"\n    %s type %s",
			    args1->is_defn? "Dummy": "Actual",
			    type_name[t1]);
#endif
		if(!defsize1)
		  (void)fprintf(list_fd,"*%d",s1);
		(void)fprintf(list_fd," %s",
			class_name[storage_class_of(a1[i].type)]);
		arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
		(void)fprintf(list_fd,"\n    %s arg %s is type %s",
			    args2->is_defn? "Dummy": "Actual",
			    a2[i].name,
			    type_name[t2]);
#else
		(void)fprintf(list_fd,"\n    %s type %s",
			    args2->is_defn? "Dummy": "Actual",
			    type_name[t2]);
#endif
		if(!defsize2)
		  (void)fprintf(list_fd,"*%d",s2);
		(void)fprintf(list_fd," %s",
			class_name[storage_class_of(a2[i].type)]);
		arg_error_locate(args2);

		if(args1->is_defn
			&& storage_class_of(a1[i].type) == class_SUBPROGRAM
			&& storage_class_of(a2[i].type) != class_SUBPROGRAM
			&& datatype_of(a1[i].type) != type_SUBROUTINE
			&& ! a1[i].declared_external )
		  (void)fprintf(list_fd,
		     "\n    (possibly it is an array which was not declared)");
	      }
				/* If no class/type/elementsize clash,
				   and if comparing dummy vs. actual,
				   check character and hollerith sizes */
	      else if(args1->is_defn) {
				/* Character: check size but skip *(*)
				   and dummy array vs. actual array element.
				 */
		if(t1 == type_STRING && s1 > 0 && s2 > 0 &&
		  !(a1[i].array_var && a2[i].array_element)) {
		    unsigned long
		      dims1,dims2,size1,size2;

		    if(a1[i].array_var) {
		      dims1 = array_dims(a1[i].info.array_dim);
		      size1 = array_size(a1[i].info.array_dim);
		    }
		    else {
		      dims1 = 0;
		      size1 = 1;
		    }
		    if(a2[i].array_var && !a2[i].array_element) {
		      dims2 = array_dims(a2[i].info.array_dim);
		      size2 = array_size(a2[i].info.array_dim);
		    }
		    else {
		      dims2 = 0;
		      size2 = 1;
		    }

				/* standard requires dummy <= actual size.
			         */
		  if( (s1*size1 > s2*size2 &&
		      (dims1==0 || size1>1) && (dims2==0 || size2>1)) ) {

		    if(cmp_error_head(name," argument mismatch"))
				break;

		    (void)fprintf(list_fd, "\n  at position %d:", i+1);
#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Dummy arg %s is type %s*%d",
			    a1[i].name,
			    type_name[t1],s1);
#else
		    (void)fprintf(list_fd,"\n    Dummy type %s*%d",
			    type_name[t1],s1);
#endif
		    if(dims1 > 0)
		      (void)fprintf(list_fd,"(%lu)",size1);
		    arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Actual arg %s is type %s*%d",
			    a2[i].name,
			    type_name[t2],s2);
#else
		    (void)fprintf(list_fd,"\n    Actual type %s*%d",
			    type_name[t2],s2);
#endif
		    if(dims2 > 0)
		      (void)fprintf(list_fd,"(%lu)",size2);
		    arg_error_locate(args2);
		  }/*end if char size mismatch*/
		}/*end if type==char*/

		else if(t2 == type_HOLLERITH) {
			/* Allow hollerith to match any noncharacter type of
			   at least equal aggregate size.  */
		    unsigned long dims1,size1;
		    if(a1[i].array_var) {
		      dims1 = array_dims(a1[i].info.array_dim);
		      size1 = array_size(a1[i].info.array_dim);
		    }
		    else {
		      dims1 = 0;
		      size1 = 1;
		    }
		    if(s2 > s1*size1 && (dims1==0 || size1>1)) {

		      if(cmp_error_head(name," argument mismatch"))
				break;

		      (void)fprintf(list_fd, "\n  at position %d:", i+1);
#ifdef KEEP_ARG_NAMES
		      (void)fprintf(list_fd,"\n    Dummy arg %s is type %s",
			    a1[i].name,
			    type_name[t1]);
#else
		      (void)fprintf(list_fd,"\n    Dummy type %s",
			    type_name[t1]);
#endif
		      if(!defsize1)
			(void)fprintf(list_fd,"*%d",s1);
		      if(dims1 > 0)
			(void)fprintf(list_fd,"(%lu)",size1);
		      arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
		      (void)fprintf(list_fd,"\n    Actual arg %s is type %s*%d",
			    a2[i].name,
			    type_name[t2],s2);
#else
		      (void)fprintf(list_fd,"\n    Actual type %s*%d",
			    type_name[t2],s2);
#endif
		      arg_error_locate(args2);
		    }/*end if holl size mismatch*/
		}/*end if type==holl*/
	      }
	    }/*end for i*/
	}/* end look for type && size mismatches */


		 /* Check arrayness of args only if defn exists */
	if(check_args_type && args1->is_defn ) {
	    cmp_error_count = 0;
	    for (i=0; i<n; i++) {
			/* Skip if class or datatype mismatch.  This
			   also skips holleriths which were checked above.
			   Do not process externals.
			 */
	      if(datatype_of(a2[i].type) != type_HOLLERITH &&
		 storage_class_of(a1[i].type) == class_VAR &&
		 storage_class_of(a2[i].type) == class_VAR) {

		if( a1[i].array_var ) {	/* I. Dummy arg is array */
		    if( a2[i].array_var ) {
			if( a2[i].array_element ) {
					/*   A. Actual arg is array elt */
					/*	Warn on check_array_dims. */
			    if(check_array_dims) {

			      if(cmp_error_head(
				      name," argument arrayness mismatch"))
				break;

			      (void)fprintf(list_fd,"\n  at position %d:", i+1);

#ifdef KEEP_ARG_NAMES
			      (void)fprintf(list_fd,
				     "\n    Dummy arg %s is whole array",
				     a1[i].name);
#else
			      (void)fprintf(list_fd,"\n    Dummy arg is whole array");
#endif
			      arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
			      (void)fprintf(list_fd,
				    "\n    Actual arg %s is array element",
				    a2[i].name);
#else
			      (void)fprintf(list_fd,"\n    Actual arg is array element");
#endif
			      arg_error_locate(args2);
			    }
			}/* end case I.A. */

			else {
					/*   B. Actual arg is whole array */
					/*	Warn if dims or sizes differ */
			  unsigned long
			    diminfo1,diminfo2,dims1,dims2,size1,size2,
			    cmpsize1,cmpsize2;
			  diminfo1 = a1[i].info.array_dim;
			  diminfo2 = a2[i].info.array_dim;
			  dims1 = array_dims(diminfo1);
			  dims2 = array_dims(diminfo2);
			  cmpsize1 = size1 = array_size(diminfo1);
			  cmpsize2 = size2 = array_size(diminfo2);
				/* For char arrays relevant size is no. of
				   elements times element size. But use
				   no. of elements if *(*) involved. */
			  if(datatype_of(a1[i].type) == type_STRING
			     && a1[i].size > 0 && a2[i].size > 0) {
			    cmpsize1 *= a1[i].size;
			    cmpsize2 *= a2[i].size;
			  }

			/* size = 0 or 1 means variable-dim: OK to differ */
			  if( (check_array_size &&
				  (size1>1 && size2>1 && cmpsize1 != cmpsize2))
			     || (check_array_dims &&
				  (dims1 != dims2)) ) {


				if(cmp_error_head(
					name," argument arrayness mismatch"))
				      break;

				(void)fprintf(list_fd,"\n  at position %d:", i+1);

#ifdef KEEP_ARG_NAMES
				(void)fprintf(list_fd,
					"\n    Dummy arg %s has %ld dim%s size %ld",
					a1[i].name,
					dims1,pluralize(dims1),
					size1);
#else
				(void)fprintf(list_fd,
					"\n    Dummy arg %ld dim%s size %ld",
					dims1,pluralize(dims1),
					size1);
#endif
				if(datatype_of(a1[i].type) == type_STRING &&
				   a1[i].size > 0)
				  (void)fprintf(list_fd,"*%ld",a1[i].size);
				arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
				(void)fprintf(list_fd,
					"\n    Actual arg %s has %ld dim%s size %ld",
					a2[i].name,
					dims2,pluralize(dims2),
					size2);
#else
				(void)fprintf(list_fd,
					"\n    Actual arg %ld dim%s size %ld",
					dims2,pluralize(dims2),
					size2);
#endif
				if(datatype_of(a2[i].type) == type_STRING
				   && a2[i].size > 0)
				  (void)fprintf(list_fd,"*%ld",a2[i].size);
				arg_error_locate(args2);
			  }/* end if size mismatch */
			}/* end case I.B. */
		    }
		    else {
					/*   C. Actual arg is scalar */
					/*	Warn in all cases */

		      	if(cmp_error_head(
				name," argument arrayness mismatch"))
			  break;

			(void)fprintf(list_fd,"\n  at position %d:", i+1);

#ifdef KEEP_ARG_NAMES
			(void)fprintf(list_fd,"\n    Dummy arg %s is array",
				      a1[i].name);
#else
			(void)fprintf(list_fd,"\n    Dummy arg is array");
#endif
			arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
			(void)fprintf(list_fd,"\n    Actual arg %s is scalar",
				      a2[i].name);
#else
			(void)fprintf(list_fd,"\n    Actual arg is scalar");
#endif
			arg_error_locate(args2);
		    }/* end case I.C. */
		} /* end dummy is array, case I. */

		else {			/* II. Dummy arg is scalar */
		    if( a2[i].array_var ) {
			if( a2[i].array_element ) {
					/*   A. Actual arg is array elt */
					/*	OK */
			}
			else {
					/*   B. Actual arg is whole array */
					/*	Warn in all cases */

			  if(cmp_error_head(
				   name," argument arrayness mismatch"))
			    break;

			  (void)fprintf(list_fd,"\n  at position %d:", i+1);

#ifdef KEEP_ARG_NAMES
			  (void)fprintf(list_fd,
			      "\n    Dummy arg %s is scalar",
			      a1[i].name);
#else
			  (void)fprintf(list_fd,"\n    Dummy arg is scalar");
#endif
			  arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
			  (void)fprintf(list_fd,
			      "\n    Actual arg %s is whole array",
			       a2[i].name);
#else
			  (void)fprintf(list_fd,"\n    Actual arg is whole array");
#endif
			  arg_error_locate(args2);

			}/* end case II.B. */
		    }
		    else {
					/*   C. Actual arg is scalar */
					/*	OK */
		    }

		} /* end dummy is scalar, case II */

	      } /* end if class_VAR */
	    }/* end for (i=0; i<n; i++) */
	}/* if( args1->is_defn ) */


		 /* Check usage of args only if defn exists */
	if(check_var_set_used && args1->is_defn) {

	    cmp_error_count = 0;
	    for (i=0; i<n; i++) {
	      if(storage_class_of(a1[i].type) == class_VAR &&
		 storage_class_of(a2[i].type) == class_VAR ) {
		int nonlvalue_out = (a1[i].assigned_flag && !a2[i].is_lvalue),
		    nonset_in = (a1[i].used_before_set && !a2[i].set_flag);

#if DEBUG_PGSYMTAB
if(debug_latest) {
(void)fprintf(list_fd,
"\nUsage check: %s[%d] dummy asgnd %d ubs %d  actual lvalue %d set %d",
args1->module->name,
i+1,
a1[i].assigned_flag,
a1[i].used_before_set,
a2[i].is_lvalue,
a2[i].set_flag);
}
#endif

		if(nonlvalue_out || nonset_in) {

		  if(cmp_error_head(name," argument usage mismatch"))
		     break;

		  (void)fprintf(list_fd,"\n  at position %d:", i+1);

		  if(nonlvalue_out) {
#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Dummy arg %s is modified",
				  a1[i].name);
#else
		    (void)fprintf(list_fd,"\n    Dummy arg is modified");
#endif
		    arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Actual arg %s is const or expr",
				  a2[i].name);
#else
		    (void)fprintf(list_fd,"\n    Actual arg is const or expr");
#endif
		    arg_error_locate(args2);
		  }
		  else if(nonset_in) {

#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Dummy arg %s used before set",
				  a1[i].name);
#else
		    (void)fprintf(list_fd,"\n    Dummy arg used before set");
#endif
		    arg_error_locate(args1);

#ifdef KEEP_ARG_NAMES
		    (void)fprintf(list_fd,"\n    Actual arg %s not set",
				  a2[i].name);
#else
		    (void)fprintf(list_fd,"\n    Actual arg not set");
#endif
		    arg_error_locate(args2);
		  }
		}
	      }
	    }
	}/*end if(check_var_set_used && args->is_defn) */

}/* arg_array_cmp */



void
check_arglists(VOID)	/* Scans global symbol table for subprograms */
{                       /* and finds subprogram defn if it exists */
	unsigned i;
	ArgListHeader *defn_list, *alist;

	for (i=0; i<glob_symtab_top; i++){

				/* Skip common blocks */
	    if(storage_class_of(glob_symtab[i].type) != class_SUBPROGRAM)
		continue;

				/* Skip unvisited library modules */
	    if(glob_symtab[i].library_module && !glob_symtab[i].visited)
		continue;


	    if((alist=glob_symtab[i].info.arglist) == NULL){
	      oops_message(OOPS_NONFATAL,NO_LINE_NUM,NO_COL_NUM,
		      "global symbol has no argument lists:");
	      oops_tail(glob_symtab[i].name);
	    }
	    else{	/* alist != NULL */
		int num_defns= 0;
		ArgListHeader *list_item;

			/* use 1st invocation instead of defn if no defn */
		defn_list = alist;

				/* Find a definition in the linked list of
				   usages.  Count how many defns found. */
		list_item = alist;
		while(list_item != NULL){
		    if(list_item->is_defn){
			if(check_ext_set_used && num_defns > 0) {/* multiple defn */
			    if(num_defns == 1) {
			      (void)fprintf(list_fd,
				      "\nSubprogram %s multiply defined:\n    ",
				      glob_symtab[i].name);
			      arg_error_locate(defn_list);
			    }
			    (void)fprintf(list_fd,"\n    ");
			    arg_error_locate(list_item);
			}
			++num_defns;
			defn_list = list_item;	/* Use last defn found */
		    }
		    else { /* ! list_item->is_defn */
				/* Here treat use as actual arg like call */
			if(list_item->is_call || list_item->actual_arg){
				 /* Use last call by a visited or nonlibrary
				    module as defn if no defn found */
			  if(!defn_list->is_defn
			     && !irrelevant(list_item) )
			    defn_list = list_item;
		        }
		    }

		    list_item = list_item->next;
		}
		if(num_defns == 0){
				/* If no defn found, and all calls are
				   from unvisited library modules, skip. */
		  if(irrelevant(defn_list))
		    continue;
				/* If no definitions found, report error
				   unless -noext is given */
		   if(check_ext_set_used) {
		     (void)fprintf(list_fd,
			     "\nSubprogram %s never defined",
			     glob_symtab[i].name);
		     if(!glob_symtab[i].used_flag)
		       (void)fprintf(list_fd," nor invoked");

		     (void)fprintf(list_fd, "\n    %s",
			     (defn_list->external_decl)?"declared":"invoked");
		     arg_error_locate(defn_list);

			/* Warn if it seems it may just be an array they
			   forgot to declare */
		      if(defn_list->numargs != 0
			 && datatype_of(defn_list->type) != type_SUBROUTINE
			 && ! glob_symtab[i].declared_external) {
			if(novice_help)
			  (void)fprintf(list_fd,
	    "\n    (possibly it is an array which was not declared)");
		      }
		   }
		}
				/* If definition is found but module is
				   not in call tree, report it unless -lib */
		else{	/* num_defns != 0 */
		    if(!glob_symtab[i].visited
		       && datatype_of(glob_symtab[i].type) != type_BLOCK_DATA
		       && !glob_symtab[i].library_module
		       && check_ext_unused ) {
			(void)fprintf(list_fd,"\nSubprogram %s never invoked",
				glob_symtab[i].name);
			(void)fprintf(list_fd, "\n    defined");
			arg_error_locate(defn_list);
		    }
		}

			/* Now check defns/invocations for consistency.  If
			   no defn, 1st invocation will serve. Here treat
			   use as actual arg like call.  Ignore calls & defns
			   in unvisited library modules. */
		if( check_args_type &&
		   (defn_list->is_defn || !defn_list->external_decl)) {
		  while(alist != NULL){
			int typerrs = 0;
			if(alist != defn_list && !alist->external_decl
			   && !irrelevant(alist)) {
			  int c1 = storage_class_of(defn_list->type),
			      c2 = storage_class_of(alist->type),
			      t1 = datatype_of(defn_list->type),
			      t2 = datatype_of(alist->type),
			      s1 = defn_list->size,
			      s2 = alist->size,
			      defsize1 = (s1 == size_DEFAULT),
			      defsize2 = (s2 == size_DEFAULT),
			      cmptype1= type_category[t1],
			      cmptype2= type_category[t2];
		/* If -portability, do not translate default sizes so
		   they will never match explicit sizes. */
			  if(!(port_mixed_size || local_wordsize==0)) {
			    if(defsize1)
			      s1 = type_size[t1];
			    if(defsize2)
			      s2 = type_size[t2];
			  }

			  if(s1 < 0 || s2 < 0){ /*size_ADJUSTABLE or UNKNOWN*/
			    s1 = s2 = size_DEFAULT;/* suppress size warnings */
			    defsize1 = defsize2 = TRUE;
			  }
				/* Check class, type, and size */
			  if( (c1 != c2) || (cmptype1 != cmptype2) ||
			     ( (s1 != s2) &&
				/*exclude char size-only mismatch betw calls */
			      (t1 != type_STRING ||
			        defn_list->is_defn || alist->is_defn )) ){

			    	if(typerrs++ == 0){
				  (void)fprintf(list_fd,
				    "\nSubprogram %s invoked inconsistently:",
				     glob_symtab[i].name);
				  (void)fprintf(list_fd,
				    "\n    %s type %s",
				    defn_list->is_defn? "Defined":"Invoked",
				    type_name[t1]);
				  if(!defsize1)
				    (void)fprintf(list_fd,"*%d",s1);
				  arg_error_locate(defn_list);
				}
				(void)fprintf(list_fd,
				    "\n    %s type %s",
				    alist->is_defn? "Defined":"Invoked",
				    type_name[t2]);
				if(!defsize2)
				  (void)fprintf(list_fd,"*%d",s2);
				arg_error_locate(alist);
			  }
			}
			alist = alist->next;

		  }/* end while(alist != NULL) */
	        }/* end if(defn) */

		alist = glob_symtab[i].info.arglist;
		while(alist != NULL){
		  /* Here we require true call, not use as actual arg.
		     Also, do not compare multiple defns against each
		     other. */
		    if(alist != defn_list &&
		       (defn_list->is_defn || defn_list->is_call) &&
		       (alist->is_call && !irrelevant(alist)) ){
			    arg_array_cmp(glob_symtab[i].name,defn_list,alist);
			}
			alist = alist->next;

		}/* end while(alist != NULL) */
	    }/* end else <alist != NULL> */
	}/* end for (i=0; i<glob_symtab_top; i++) */
}


void
check_comlists(VOID)        /* Scans global symbol table for common blocks */
{
	unsigned i, model_n;
	ComListHeader *first_list, *model, *clist;

				/* Check for name clashes with subprograms */
	if(f77_common_subprog_name) {
	  check_nameclash();
	}

	if(check_com_off)
		return;

	for (i=0; i<glob_symtab_top; i++){

	    if (storage_class_of(glob_symtab[i].type) != class_COMMON_BLOCK)
		continue;

	    if((first_list=glob_symtab[i].info.comlist) == NULL){
		(void)fprintf(list_fd,"\nCommon block %s never defined",
			glob_symtab[i].name);
	    }
	    else {
		      /* Find instance with most variables to use as model */
		model=first_list;
		model_n = first_list->numargs;
		clist = model;
		while( (clist=clist->next) != NULL ){
		    if(clist->numargs >= model_n /* if tie, use earlier */
			/* also if model is from an unvisited library
			   module, take another */
		       || irrelevant(model) ) {
			model = clist;
			model_n = clist->numargs;
		    }
		}

		if( irrelevant(model) )
		  continue;	/* skip if irrelevant */

			/* Check consistent SAVEing of block:
			   If SAVEd in one module, must be SAVEd in all.
			   Main prog is an exception: SAVE ignored there. */
	      {
		ComListHeader *saved_list, *unsaved_list;
		saved_list = unsaved_list = (ComListHeader *)NULL;
		clist = first_list;
		while( clist != NULL ){

		    if(!irrelevant(clist) && clist->module->type !=
		       type_byte(class_SUBPROGRAM,type_PROGRAM) ) {

		      if(clist->saved)
			saved_list = clist;
		      else
			unsaved_list = clist;
		    }
		    clist = clist->next;
		}
		if(saved_list != (ComListHeader *)NULL &&
		   unsaved_list != (ComListHeader *)NULL) {
			  (void)fprintf(list_fd,
				"\nCommon block %s not SAVED consistently",
				glob_symtab[i].name);
			  (void)fprintf(list_fd,
				  "\n    is SAVED");
			  com_error_locate(saved_list);
			  (void)fprintf(list_fd,
				  "\n    is not SAVED");
			  com_error_locate(unsaved_list);
		}
	      }


				/* Now check agreement of common lists */
		clist = first_list;
		while( clist != NULL ){
		    if(clist != model && !irrelevant(clist)) {

			if(check_com_byname)
			  com_cmp_strict(glob_symtab[i].name,model,clist);
			else
			  com_cmp_lax(glob_symtab[i].name,model,clist);
		    }
		    clist = clist->next;
		}
	    }
	}
} /* check_comlists */



PRIVATE void
#if HAVE_STDC
com_cmp_lax(char *name, ComListHeader *c1, ComListHeader *c2)		/* Common-list check at levels 1 & 2 */
#else /* K&R style */
com_cmp_lax(name,c1,c2)		/* Common-list check at levels 1 & 2 */
     char *name;
     ComListHeader *c1,*c2;
#endif /* HAVE_STDC */
{
    int i1,i2,			/* count of common variables in each block */
	done1,done2,		/* true when end of block reached */
	type1,type2;		/* type of variable presently in scan */
    unsigned long
	len1,len2,		/* length of variable remaining */
        size1,size2,		/* unit size of variable */
	word1,word2,		/* number of "words" scanned */
	words1,words2,		/* number of "words" in block */
        defsize1,defsize2,	/* default size used? */
	jump;			/* number of words to skip next in scan */
    int byte_oriented=FALSE,	/* character vs numeric block */
        type_clash;		/* flag for catching clashes */
    int n1=c1->numargs,n2=c2->numargs; /* variable count for each block */
    int numerrs;
    ComListElement *a1=c1->com_list_array, *a2=c2->com_list_array;

				/* Count words in each list */
    words1=words2=0;
    for(i1=0; i1<n1; i1++) {
      size1 = a1[i1].size;
      if(size1 == size_DEFAULT)
	size1 = type_size[a1[i1].type];
      else
	byte_oriented = TRUE;
      words1 += array_size(a1[i1].dimen_info)*size1;
    }
    for(i2=0; i2<n2; i2++) {
      size2 = a2[i2].size;
      if(size2 == size_DEFAULT)
	size2 = type_size[a2[i2].type];
      else
	byte_oriented = TRUE;
      words2 += array_size(a2[i2].dimen_info)*size2;
    }
	/* If not byte oriented, then sizes are all multiples of
	   BpW and can be reported as words according to F77 std. */
    if(!byte_oriented) {
      words1 /= BpW;
      words2 /= BpW;
    }
    if(check_com_lengths && words1 != words2) {
      (void)fprintf(list_fd,
	      "\nCommon block %s: varying length:", name);
      (void)fprintf(list_fd,
	      "\n    Has %ld %s%s",
		words1,
		byte_oriented? "byte":"word",
		pluralize(words1));
      com_error_locate(c1);
      (void)fprintf(list_fd,
	      "\n    Has %ld %s%s",
		words2,
		byte_oriented? "byte":"word",
		pluralize(words2));
      com_error_locate(c2);
    }

				/* Now check type matches */
    done1=done2=FALSE;
    i1=i2=0;
    len1=len2=0;
    word1=word2=1;
    numerrs=0;
    for(;;) {
	if(len1 == 0) {		/* move to next variable in list 1 */
	    if(i1 == n1) {
		done1 = TRUE;
	    }
	    else {
		type1 = a1[i1].type;
		size1 = a1[i1].size;
		defsize1 = (size1 == size_DEFAULT);
		if(defsize1)
		  size1 = type_size[type1];
		if(!byte_oriented)
		  size1 /= BpW;	/* convert bytes to words */
		len1 = array_size(a1[i1].dimen_info)*size1;
		++i1;
	    }
	}
	if(len2 == 0) {		/* move to next variable in list 2 */
	    if(i2 == n2) {
		done2 = TRUE;
	    }
	    else {
		type2 = a2[i2].type;
		size2 = a2[i2].size;
		defsize2 = (size2 == size_DEFAULT);
		if(defsize2)
		  size2 = type_size[type2];
		if(!byte_oriented)
		  size2 /= BpW;
		len2 = array_size(a2[i2].dimen_info)*size2;
		++i2;
	    }
	}

	if(done1 || done2){	/* either list exhausted? */
	    break;		/* then stop checking */
	}

		/* Look for type clash.  Allow explicitly sized real to
		   match double of equal size.
		   Allow real to match complex whose parts are of equal size.
		   Within same type category, size diff counts as clash
		   except with char.
		   Also issue warning under -portability or -nowordsize
		   if an explicit size is matched to an implicit size. */
	type_clash = FALSE;
	if( (type_category[type1] == type_category[type2]) ) {
	  if( type1 != type_STRING &&
	      (size1 != size2
	       || ((port_mixed_size || local_wordsize==0) &&
		   defsize1 != defsize2))) {
	    type_clash = TRUE;
	  }
	}
	else /* different type categories */ {
				/* Equiv_type matches complex to real */
	  if(equiv_type[type1] != equiv_type[type2]) {
	    type_clash = TRUE;
	  }
	  else {
	    if( type_category[type1] == type_COMPLEX ) {
	      type_clash = (size1 != 2*size2);
	    }
	    else {
				/* 2nd block has complex */
	      type_clash = (size2 != 2*size1);
	    }
	  			/* Give warning anyway if default size
				   is matched to explicit. */
	    if( (port_mixed_size || local_wordsize==0)
	       && defsize1 != defsize2 )
	      type_clash = TRUE;
	  }
	}

	if(type_clash) {
	     if(++numerrs > 3) {
	       (void)fprintf(list_fd,"\netc...");
	       break;		/* stop checking after third mismatch */
	     }
	     if(numerrs == 1)
	       (void)fprintf(list_fd,
		       "\nCommon block %s: data type mismatch",
		       name);
	     (void)fprintf(list_fd,"\n    %s %ld is type %s",
		     byte_oriented?"Byte":"Word",
		     word1,
		     type_name[type1]);
	     if(!defsize1)
	       (void)fprintf(list_fd,"*%lu",
		       size1);
	     com_error_locate(c1);

	     (void)fprintf(list_fd,"\n    %s %ld is type %s",
		     byte_oriented?"Byte":"Word",
		     word2,
		     type_name[type2]);
	     if(!defsize2)
	       (void)fprintf(list_fd,"*%lu",
		       size2);
	     com_error_locate(c2);
	}

			/* Advance along list by largest possible
			   step that does not cross a variable boundary.
			   If matching complex to real, only advance
			   the real part.
			 */
	jump = len1 < len2? len1: len2;	/* min(len1,len2) */
	len1 -= jump;
	len2 -= jump;
	word1 += jump;
	word2 += jump;
    }/* end for(;;) */
}

PRIVATE void
#if HAVE_STDC
com_cmp_strict(char *name, ComListHeader *c1, ComListHeader *c2)	/* Common-list check at level 3 */
#else /* K&R style */
com_cmp_strict(name,c1,c2)	/* Common-list check at level 3 */
	char *name;
	ComListHeader *c1, *c2;
#endif /* HAVE_STDC */
{
	int i,
	    typerr,		/* count of type/size mismatches */
	    dimerr;		/* count of array dim/size mismatches */
	short n,
	      n1 = c1->numargs,
	      n2 = c2->numargs;
	ComListElement *a1 = c1->com_list_array,
		       *a2 = c2->com_list_array;

	n = (n1 > n2) ? n2: n1;
	if(n1 != n2){
	  (void)fprintf(list_fd,
		  "\nCommon block %s: varying length:", name);
	  (void)fprintf(list_fd,
		  "\n    Has %d variable%s",
		  n1,pluralize(n1));
	  com_error_locate(c1);

	  (void)fprintf(list_fd,
		  "\n    Has %d variable%s",
		  n2,pluralize(n2));
	  com_error_locate(c2);
        }
#if DEBUG_PGSYMTAB
if(debug_latest){
(void)fprintf(list_fd,"block %s",name);
(void)fprintf(list_fd,"\n\t1=in module %s line %u file %s (%s)",
		    c1->module->name,
		    c1->line_num,
		    c1->topfile
	            c1->filename);
(void)fprintf(list_fd,"\n\t2=in module %s line %u file %s (%s)",
		    c2->module->name,
		    c2->line_num,
		    c2->topfile,
	            c2->filename);
}
#endif
	typerr = 0;
	for (i=0; i<n; i++) {
	  int t1 = datatype_of(a1[i].type),
	      t2 = datatype_of(a2[i].type),
	      s1 = a1[i].size,
	      s2 = a2[i].size,
	      defsize1 = (s1==size_DEFAULT),
	      defsize2 = (s2==size_DEFAULT);
		/* If -portability, do not translate default sizes so
		   they will never match explicit sizes. */
	 if(!(port_mixed_size || local_wordsize==0)) {
	   if(defsize1)
	     s1 = type_size[t1];
	   if(defsize2)
	     s2 = type_size[t2];
	 }

	    if( t1 != t2 || s1 != s2 ) {
				/* stop after limit: probably a cascade */
			if(++typerr > CMP_ERR_LIMIT) {
				(void)fprintf(list_fd,"\n etc...");
				break;
			}

		        if(typerr == 1)
			  (void)fprintf(list_fd,
				  "\nCommon block %s: data type mismatch",
				  name);
			(void)fprintf(list_fd, "\n  at position %d:", i+1);

			(void)fprintf(list_fd,
				"\n    Variable %s has type %s",
				a1[i].name,
				type_name[t1]);
			if(!defsize1)
			  (void)fprintf(list_fd,"*%d",s1);
			com_error_locate(c1);

			(void)fprintf(list_fd,
				"\n    Variable %s has type %s",
				a2[i].name,
 				type_name[t2]);
			if(!defsize2)
			  (void)fprintf(list_fd,"*%d",s2);
			com_error_locate(c2);

		}/*end if(type or size mismatch)*/
	}/*end for(i=0; i<n; i++)*/


	dimerr = 0;
	for (i=0; i<n; i++){
		unsigned long d1, d2, s1, s2;

		if((d1=array_dims(a1[i].dimen_info)) !=
			(d2=array_dims(a2[i].dimen_info))){

				/* stop after limit: probably a cascade */
			if(++dimerr > CMP_ERR_LIMIT) {
				(void)fprintf(list_fd,"\n etc...");
				break;
			}
			if(dimerr == 1)
			  (void)fprintf(list_fd,
			      "\nCommon block %s: array dimen/size mismatch",
			      name);
			(void)fprintf(list_fd, "\nat position %d:", i+1);

			(void)fprintf(list_fd,
				"\n    Variable %s has %ld dimension%s",
				a1[i].name,
				d1,pluralize(d1));
			com_error_locate(c1);

			(void)fprintf(list_fd,
				"\n    Variable %s has %ld dimension%s",
				a2[i].name,
				d2,pluralize(d2));
			com_error_locate(c2);
		}/*end if(num dims mismatch)*/

		if((s1=array_size(a1[i].dimen_info)) !=
			(s2=array_size(a2[i].dimen_info))){

				/* stop after limit: probably a cascade */
			if(++dimerr > CMP_ERR_LIMIT) {
				(void)fprintf(list_fd,"\n etc...");
				break;
			}
			if(dimerr == 1)
			  (void)fprintf(list_fd,
			      "\nCommon block %s: array dimen/size mismatch",
				  name);
			(void)fprintf(list_fd,
				"\nat position %d:", i+1);

			(void)fprintf(list_fd,
				"\n    Variable %s has size %ld",
				a1[i].name,
				s1);
			com_error_locate(c1);

			(void)fprintf(list_fd,
				"\n    Variable %s has size %ld",
				a2[i].name,
				s2);
			com_error_locate(c2);

		}/*end if(array size mismatch)*/
	}/*end for(i=0; i<n; i++)*/

}/*com_cmp_strict*/


/**  Common block and common variable usage checks.  Implemented
 **  by John Quinn, Jan-May 1993.  Some modifications made by RKM.
 **/


void
check_com_usage(VOID)
{
#ifdef DYNAMIC_TABLES		/* tables will be mallocked at runtime */
    Gsymtab  **gsymlist;
#else
    Gsymtab  *gsymlist[GLOBSYMTABSZ];
#endif
    int  i,numentries,numblocks;
    ComListHeader  *cmlist;

#ifdef DYNAMIC_TABLES
      if( (gsymlist=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL) {
	  oops_message(OOPS_FATAL,NO_LINE_NUM,NO_COL_NUM,
		       "Cannot malloc space for common block list");
      }
#endif

				/* Print cross-reference list */
    if(print_xref_list) {
	for(i=numblocks=0;i<glob_symtab_top;i++){ /* loop thru global table */
	   if (storage_class_of(glob_symtab[i].type) == class_COMMON_BLOCK){

	     cmlist = glob_symtab[i].info.comlist;
	     numentries=0;

#ifdef DEBUG_COM_USAGE
	     (void)fprintf(list_fd, "\n Common Block %s:\n",glob_symtab[i].name );
#endif

	     while (cmlist != NULL){ /* loop thru declarations */

	         if(! irrelevant(cmlist)  &&
		    (cmlist->any_used || cmlist->any_set))
		   gsymlist[numentries++] = cmlist->module;
#ifdef DEBUG_COM_USAGE
		 print_comvar_usage(cmlist);
#endif
		 cmlist = cmlist->next;

	      }  /* end of while */

	     if (numentries >0){ /* print modules that declare this block*/

	       if(numblocks++ == 0)
		 (void)fprintf(list_fd,
		       "\n        Common block cross-reference list:\n");

	       (void)fprintf(list_fd, "\nCommon Block %s used in:\n" ,
			glob_symtab[i].name );

	       sort_gsymbols(gsymlist,numentries);

	       print_modules((unsigned)numentries,gsymlist);

	     }  /* end of if */


	   } /* end of if */

	} /* end of for */

	if(numblocks > 0)
	  (void)fprintf(list_fd,"\n");

    }/* end if print_xref_list */

				/* Print out usage info */
    if(com_usage_check > 0) {
	for(i=0;i<glob_symtab_top;i++){ /* loop thru global table */
	   if (storage_class_of(glob_symtab[i].type) == class_COMMON_BLOCK){

	       com_block_usage(glob_symtab[i].name,
				 glob_symtab[i].info.comlist );
	   }
	}
    }
#ifdef DYNAMIC_TABLES
    (void) cfree(gsymlist);
#endif
}

		/* Routine to check for common block having same name
		   as subprogram, which is nonstandard.  */
PRIVATE void
check_nameclash(VOID)
{
  int i;
  ArgListHeader *alist;
  for(i=0;i<HASHSZ;i++) {
    if(hashtab[i].glob_symtab != NULL &&
       hashtab[i].com_glob_symtab != NULL) {
      fprintf(list_fd,
	"\nWarning: Common block and subprogram have same name (nonstandard)");
      fprintf(list_fd,
	"\n    Common block %s declared",hashtab[i].name);
      com_error_locate(hashtab[i].com_glob_symtab->info.comlist);
      for(alist=hashtab[i].glob_symtab->info.arglist;alist!=NULL;
	  alist=alist->next) {
	if(alist->is_defn) {
	  break;
	}
      }
      /* if not declared: use first reference */
      fprintf(list_fd,"\n    Subprogram %s %s",hashtab[i].name,
	      alist==NULL?"referenced":"declared");
      arg_error_locate(
	      alist==NULL? hashtab[i].glob_symtab->info.arglist: alist);
    }
  }
}

PRIVATE void
#if HAVE_STDC
print_modules(unsigned int n, Gsymtab **list)    /* formatting of module names */
#else /* K&R style */
print_modules(n,list)    /* formatting of module names */
	unsigned n;
	Gsymtab *list[];
#endif /* HAVE_STDC */
{
	unsigned col=0,len,j;

        for (j=0;j<n;j++){
	  if(list[j]->internal_entry) {
		 len=strlen(list[j]->link.module->name);
		 col+= len= (len<=10? 10:len) +9;
		 if (col >78){
			fprintf(list_fd, "\n");
			col = len;
		 } /* end of if */
		 fprintf(list_fd,"   %10s entry",list[j]->link.module->name);
		 len=strlen(list[j]->name)+1;
		 col+= len;
		 if (col >78){
			fprintf(list_fd, "\n");
			col = len;
		 } /* end of if */
		 fprintf(list_fd," %s",list[j]->name);
	   }
	   else {
		 len=strlen(list[j]->name);
		 col+= len= (len<=10? 10:len) +3;
		 if (col >78){
			(void)fprintf(list_fd, "\n");
			col = len;
		 } /* end of if */

		 (void)fprintf(list_fd,"   %10s",list[j]->name);
	   }


	 } /* end of for */
}



#ifdef DEBUG_COM_USAGE

PRIVATE void print_comvar_usage(comlist)

	ComListHeader *comlist;
{
        int i, count;
  	ComListElement *c;

  	count = comlist->numargs;
  	c = comlist->com_list_array;

/* prints out caller module and any_used, any_set flags in CLhead */

	(void)fprintf(list_fd, "\nModule %s  any_used %u any_set %u\n",
                comlist->module->name, comlist->any_used, comlist->any_set);

        if((comlist->any_used || comlist-> any_set||1) ){
           for (i=0; i<count; i++){

/* prints out all four flags for each element in array */

              (void)fprintf(list_fd,
		"\n Element %d (%s) used %u set %u used bf set %u asgnd %u\n"
		      , i+1
		      , c[i].name
		      , c[i].used
		      , c[i].set
		      , c[i].used_before_set
		      , c[i].assigned);
	   } /* end of for */

        } /* end of if */
}
#endif

	/* Check used, set status of common block.  First it looks for
	   whether the block is totally unused, and if so prints a warning
	   and returns.  Otherwise, if block is unused by some modules,
	   it says which ones.  Meanwhile, it finds the declaration with
	   the most elements to use as reference.  If common strictness
	   is 3 (variable by variable) then it OR's the usage flags of
	   each block variable among different declarations, saving the
	   result in reference list.  Passes off to com_element_usage
	   to print usage of individual common variables.
	   */
PRIVATE int any_com_warning;
#define IDENTIFY_COMBLOCK if(any_com_warning++ == 0) \
		(void)fprintf(list_fd,"\nCommon block %s:",name)

PRIVATE void
#if HAVE_STDC
com_block_usage(char *name, ComListHeader *cl1)
#else /* K&R style */
com_block_usage(name,cl1)
     char *name;
     ComListHeader *cl1;
#endif /* HAVE_STDC */
{
     ComListHeader *ref_cl,	/* reference decl: has most elements */
     	*cur_cl;		/* running cursor thru decls  */
     int j,n,ref_n;
     int block_any_used, block_any_set;
     int block_unused_somewhere;
     ComListElement *ref_list, *c;

	any_com_warning = 0; /* used to print block name once only */

        block_any_used = block_any_set = FALSE;
	block_unused_somewhere = FALSE;
	ref_n = cl1->numargs;
        ref_cl= cl1;
	cur_cl = cl1;
	while (cur_cl!=NULL){  /* traverses CLheads */
	  if(! irrelevant(cur_cl) ) {

            if (cur_cl->any_used){  /* stores TRUE if any are TRUE */
		block_any_used = TRUE;
            }
	    if (cur_cl->any_set){   /* stores TRUE if any are TRUE */
		block_any_set = TRUE;
	    }
	    if( ! (cur_cl->any_used || cur_cl->any_set) &&
		! cur_cl->module->defined_in_include ) {
	      block_unused_somewhere = TRUE;
	    }
   /* if any_set and any_used false after this loop block never used */

	    if (cur_cl->numargs > ref_n){ /* find largest array */
		ref_cl = cur_cl;
		ref_n = cur_cl->numargs;
            } /* end of if */
	  }/* end if not irrelevant */
	  cur_cl = cur_cl->next;
	}

        if(irrelevant(ref_cl))	/* Block not declared by modules in calltree */
	  return;

     if(! (block_any_used || block_any_set) ) {	/* Totally unused */
       if(check_com_unused) {
	 IDENTIFY_COMBLOCK;
	 (void)fprintf(list_fd," unused");
       }
     }
     else {
				/* If block used somewhere but not everywhere,
				   report it. */
        if(block_unused_somewhere && check_com_unused) {
	  IDENTIFY_COMBLOCK;
	  (void)fprintf(list_fd," unused");
	  cur_cl = cl1;
	  while (cur_cl!=NULL){  /* traverses CLheads */
	    if(! irrelevant(cur_cl) ) {
	      if( ! (cur_cl->any_used || cur_cl->any_set) &&
		  ! cur_cl->module->defined_in_include ) {
		(void)fprintf(list_fd,"\n  ");
		com_error_locate(cur_cl);
	      }
	    }
	    cur_cl = cur_cl->next;
	  }
	}/* end if block_unused_somewhere */

	if(! check_com_byname) {
				/* If not variablewise checking, just
				   give general warnings. */
	  if (!block_any_set){
	    if(check_com_set_used) {
	      IDENTIFY_COMBLOCK;
	      (void)fprintf (list_fd," No elements are set, but some are used.");
	    }
	  }
	  if (!block_any_used){
	    if(check_com_set_used) {
	      IDENTIFY_COMBLOCK;
	      (void)fprintf (list_fd," No elements are used, but some are set.");
	    }
	  }
        }
	else {	/* strictness == 3 */
				/* Now go thru the details for each element */
	  ref_list = ref_cl->com_list_array;
	  ref_cl->any_used = block_any_used;
	  ref_cl->any_set = block_any_set;

/* traversing elements in arrays and storing OR'd values in largest array*/

	  cur_cl = cl1;
	  while (cur_cl!=NULL){
	    if(! irrelevant(cur_cl) ) {
	      c = cur_cl->com_list_array;
	      n = cur_cl->numargs;
	      for (j=0; j<n; j++){
		if (c[j].used) {
		  ref_list[j].used = TRUE;
		}
		if (c[j].set){
		  ref_list[j].set = TRUE;
		}
		if (c[j].used_before_set){
		  ref_list[j].used_before_set = TRUE;
		}
		if (c[j].assigned){
		  ref_list[j].assigned = TRUE;
		}
	      }
	    }
	    cur_cl = cur_cl->next;
	  }
	  com_element_usage(name, ref_cl, ref_list, ref_n);
	}
     }
}


PRIVATE void
#if HAVE_STDC
com_element_usage(char *name, ComListHeader *r_cl, ComListElement *r_list, int r_num)
#else /* K&R style */
com_element_usage(name,  r_cl, r_list, r_num)

	char *name;
	ComListHeader *r_cl;
        ComListElement  *r_list;
	int r_num;

#endif /* HAVE_STDC */
{
	int i,col, warnings;

 	if (r_cl->any_used || r_cl->any_set){  /* if false block not used */

	    if(check_com_set_used) {
	      warnings = 0;
	      for (i=0; i<r_num; i++){ /* Count used-not-set cases */
		if (r_list[i].used && !r_list[i].set){
		  warnings++;
		}
	      }
	      if(warnings > 0) {
		IDENTIFY_COMBLOCK;
		(void)fprintf (list_fd,
			 "\n  Elements used but never set:");
		if(warnings == r_num) {
		  (void)fprintf(list_fd," all");
		}
		else {
		  for (i=0,col=30; i<r_num; i++){
		    if (r_list[i].used && !r_list[i].set){
		      if( (col += 1+(int)strlen(r_list[i].name)) > 78 ) {
			(void)fprintf(list_fd,"\n");
			col = 6;
		      }
		      (void)fprintf(list_fd, " %s",
				    r_list[i].name);
		    }
		  }
	        }
	      }
	    }

	    if(check_com_unused) {
	      warnings = 0;
	      for (i=0; i<r_num; i++){ /* Count set-not-used cases */
		if (r_list[i].set && !r_list[i].used){
		  warnings++;
		}
	      }
	      if(warnings > 0) {
		IDENTIFY_COMBLOCK;
		(void)fprintf (list_fd,
			 "\n  Elements set but never used:");
		if(warnings == r_num) {
		  (void)fprintf(list_fd," all");
		}
		else {
		  for (i=0,col=30; i<r_num; i++){
		    if (r_list[i].set && !r_list[i].used){
		      if( (col += 1+(int)strlen(r_list[i].name)) > 78 ) {
			(void)fprintf(list_fd,"\n");
			col = 6;
		      }
		      (void)fprintf (list_fd, " %s",
				     r_list[i].name);
		    }
	          }
	        }
	      }
	    }

	    warnings = 0;
	    for (i=0,col=30; i<r_num; i++){
	      if(!r_list[i].set && !r_list[i].used &&
		 !r_list[i].used_before_set){
		    if(check_com_unused) {
		      IDENTIFY_COMBLOCK;
		      if (warnings++ == 0 ){
			(void)fprintf (list_fd,
				 "\n  Elements never used, never set:");
		      }
		      if( (col += 1+(int)strlen(r_list[i].name)) > 78 ) {
			(void)fprintf(list_fd,"\n");
			col = 6;
		      }
		      (void)fprintf (list_fd, " %s",
				     r_list[i].name);
		    }
		}
	    }
	}
	else{	/* This cannot be reached if called only when block is used */
	  if(check_com_unused) {
	    IDENTIFY_COMBLOCK;
	    (void)fprintf (list_fd," not used.");
	  }
	}            /* any_used and any_set are both false */



}
/** End of common block and variable usage checks **/

				/* Things used for common undef check */
PRIVATE int com_tree_error;
PRIVATE int numvisited;

#define PRUNE_CALLTREE	(!(call_tree_options & CALLTREE_NOPRUNE))
#define SORT_CALLTREE   (!(call_tree_options & CALLTREE_NOSORT))

void
visit_children(VOID)
{
  int i,
	num_mains,		/* number of main programs */
	num_roots;		/* number of uncalled nonlibrary modules */
  Gsymtab* main_module;
  
  num_roots =  0;
  for(i=0; i<glob_symtab_top; i++) {
    if(storage_class_of(glob_symtab[i].type) == class_SUBPROGRAM
       && ! glob_symtab[i].internal_entry) {
      glob_symtab[i].link.child_list=
	sort_child_list(glob_symtab[i].link.child_list);
	/* Count defined but uncalled non-library modules for use later */
      if(glob_symtab[i].defined && !glob_symtab[i].used_flag &&
	 !glob_symtab[i].library_module)
	  ++num_roots;	/* Count tree roots for use if no mains */
    }
  }

  if(print_ref_list)
    (void)fprintf(list_fd,"\nList of subprogram references:");
#ifdef VCG_SUPPORT
  else if(print_vcg_list) {
    if(vcg_fd == stdout)
      (void)fprintf(vcg_fd,"\n");
    (void)fprintf(vcg_fd,"graph: {\ntitle: \"%s\"\n",main_filename);
			/* Global graph options go here.  See ftnchek.h.
			*/
    (void)fprintf(vcg_fd,VCG_GRAPH_OPTIONS);
  }
#endif
  else if(print_call_tree)
    (void)fprintf(list_fd,"\nTree of subprogram calls:");

				/* Visit children of all main progs */
  for(i=0,num_mains=0; i<glob_symtab_top; i++) {
    if(glob_symtab[i].type == type_byte(class_SUBPROGRAM,type_PROGRAM)) {
      main_module = &glob_symtab[i];
      if(print_ref_list)
	visit_child_reflist(main_module);
#ifdef VCG_SUPPORT
      else if(print_vcg_list)
	visit_child_vcg(main_module,1);
#endif
      else
	visit_child(main_module,0);
      ++num_mains;
    }
  }
				/* If no main program found, give
				   warning unless -noextern was set */
  if(num_mains == 0) {
    if(print_call_tree || print_ref_list
#ifdef VCG_SUPPORT
       || print_vcg_list
#endif
       ) {
      (void)fprintf(list_fd,"\n  (no main program found)");
    }
    else if(check_ext_set_used) {
      (void)fprintf(list_fd,
	"\nNo main program found");
    }
		/* If no main, visit trees rooted at uncalled
		   nonlibrary routines, as the next best thing.
		   If there are no uncalled nonlib modules, use
		   uncalled library routines.  If there are no uncalled
		   routines, then there is a cycle!
		 */
    for(i=0; i<glob_symtab_top; i++) {
      if(storage_class_of(glob_symtab[i].type) == class_SUBPROGRAM
	&& glob_symtab[i].defined && !glob_symtab[i].used_flag &&
	 (num_roots == 0 || !glob_symtab[i].library_module) ) {
	if(print_ref_list)
	  visit_child_reflist(&glob_symtab[i]);
#ifdef VCG_SUPPORT
	else if(print_vcg_list)
	  visit_child_vcg(&glob_symtab[i],1);
#endif
	else
	  visit_child(&glob_symtab[i],1); /* indent all trees one level */
      }
    }
  }
  if(print_call_tree || print_ref_list)
    (void)fprintf(list_fd,"\n");
#ifdef VCG_SUPPORT
  if(print_vcg_list)
    (void)fprintf(vcg_fd,"}\n");
#endif


			/* Print list of callers of all visited
			   or non-library modules, if -crossref
			   flag given. */
  if(print_xref_list) {
    print_crossrefs();
  }

			/* Print linkage-order list of modules. */
  if( print_topo_sort ) {
    (void) toposort(glob_symtab,(int)glob_symtab_top);
  }

			/* Check that common blocks retain definition
			   status between uses. */
  if(check_com_tree || check_volatile_com){
    if(num_mains != 1) {
      if(check_com_tree)
	(void)fprintf(list_fd,
		"\nCommon definition check requires single main program");
      if(check_volatile_com)
	(void)fprintf(list_fd,
		"\nCommon volatility check requires single main program");
    }
    else {
      numvisited = 0;		/* need headcount in case of cycle */
      for(i=0; i<glob_symtab_top; i++) {
	if(glob_symtab[i].visited_somewhere)
	  numvisited++;
      }
      for(i=0; i<glob_symtab_top; i++) {
	if(storage_class_of(glob_symtab[i].type) == class_COMMON_BLOCK) {
	  if( block_is_volatile(glob_symtab[i].info.comlist,main_module) ) {
	    if(check_volatile_com) {
	      (void)fprintf(list_fd,
		   "\nCommon block %s is volatile",
		   glob_symtab[i].name);
	    }
	    if(check_com_tree) {
	      com_tree_error=0;
	      (void)com_tree_check(&glob_symtab[i],main_module,0);
	    }
	  }
	}
      }
    }
  }
}

	/* Returns TRUE unless block is SAVED by any module, or declared by
	   the actual main program or in a BLOCK DATA subprogram. */
PRIVATE int
#if HAVE_STDC
block_is_volatile(ComListHeader *clist, Gsymtab *main_module)
#else /* K&R style */
block_is_volatile(clist,main_module)
     ComListHeader *clist;
     Gsymtab *main_module;
#endif /* HAVE_STDC */
{
  int t;
  while(clist != NULL) {
    if( clist->saved ||
       (t=datatype_of(clist->module->type)) == type_BLOCK_DATA
       || (t == type_PROGRAM && clist->module == main_module)) {
      return FALSE;
    }
    clist = clist->next;
  }
  return TRUE;
}

 /* If block declared by module, returns pointer to the comlist
    header which describes it.  Otherwise returns NULL. */
PRIVATE ComListHeader *
#if HAVE_STDC
com_declared_by(Gsymtab *comblock, Gsymtab *module)
#else /* K&R style */
com_declared_by(comblock,module)
     Gsymtab *comblock,*module;
#endif /* HAVE_STDC */
{
  ComListHeader *clist=comblock->info.comlist;
  while(clist != NULL) {
    if(clist->module == module) {
      if(clist->saved) {
	com_tree_error = TRUE;	/* not so, but causes bailout */
      }
      return clist;
    }
    clist = clist->next;
  }
  return NULL;
}


		/* Checks whether common block can become undefined
		   between activations of some module that declares it.
		   Should only be done for blocks that are volatile, i.e.
		   that are not SAVED or declared in main or block_data.
		   Rules used are:
		     (1) Block is declared in two subtrees whose roots
		         are called by a given module, and not in
			 the given module itself or above.
		     (2) Block is declared and elements accessed in a module
		         called by a given module, and not declared in the
			 module itself or above.  (Module that declares it but
			 does not access elements, can be holding the
			 block active for its children.)
		   Since Rule 2 is likely to be wrong often due to Ftnchek's
		   lack of knowledge about whether a routine is invoked
		   more than once, it is suppressed for now.
		*/
PRIVATE ComListHeader *
#if HAVE_STDC
com_tree_check(Gsymtab *comblock, Gsymtab *module, int level)
#else /* K&R style */
com_tree_check(comblock,module,level)
     Gsymtab *comblock,*module;
     int level;
#endif /* HAVE_STDC */
{
  ComListHeader *clist;

	/* The following only protects against recursion.  It is not
	   a full-fledged cycle detector just a stopper. */
  if(level > numvisited) {
    (void)fprintf(list_fd,
	    "\nWarning: Call tree has a cycle containing module %s\n",
	    module->name);
    com_tree_error = TRUE;
    return NULL;
  }

		/* If this module declares the block, return its clist */
  if( (clist=com_declared_by(comblock,module)) != NULL) {
#ifdef DEBUG_SAVE
      (void)fprintf(list_fd,"\n%s declared by %s",comblock->name,module->name);
#endif
    return clist;
  }
  else {	/* Otherwise see if it is declared in subtree */
    int any_child_declares_it;
    ComListHeader *declaring_clist, *this_clist;
    ChildList *child_list;

    any_child_declares_it=FALSE;
    declaring_clist=NULL;
				/* Scan list of children */
    child_list = (module->internal_entry?module->link.module:module)
		   ->link.child_list;
    while(child_list != NULL) {
      this_clist = com_tree_check(comblock,child_list->child,level+1);
				/* Error was detected below: bail out */
      if(com_tree_error) {
	return NULL;
      }
      else if(this_clist != NULL) {
				/* Subtree contains the block */
	if(any_child_declares_it			   /* Rule 1 */
#ifdef COMTREE_RULE_2
	   || (this_clist->any_used || this_clist->any_set) /* Rule 2 */
#endif
	){
	  (void)fprintf(list_fd,
    "\nWarning: Common block %s may become undefined between activations",
	    comblock->name);
	  (void)fprintf(list_fd,"\n    ");
	  com_error_locate(this_clist);
	  if(declaring_clist != NULL && declaring_clist != this_clist) {
	    (void)fprintf(list_fd,"\n    ");
	    com_error_locate(declaring_clist);
	  }
	  (void)fprintf(list_fd,"\n        ");
	  (void)fprintf(list_fd,
		  "during activation of module %s",
		  module->name);
	  com_tree_error = TRUE;
	  return NULL;
	}
	else {
	  any_child_declares_it = TRUE;
	  declaring_clist = this_clist;
	}
      }

      child_list = child_list->next;
    }
		/* If any subtree declares it, say so */
    return declaring_clist;
  }
}



				/* Depth-first search of call tree */
PRIVATE void
#if HAVE_STDC
visit_child(Gsymtab *gsymt, int level)
#else /* K&R style */
visit_child(gsymt,level)
     Gsymtab *gsymt;
     int level;
#endif /* HAVE_STDC */
{
  static char fmt[]="%000s";	/* Variable format for indenting names */
  ChildList *child_list;


  if(print_call_tree) {
    (void)fprintf(list_fd,"\n");
    if(level > 0) {
      (void)sprintf(fmt,"%%%ds",level*4); /* indent 4 spaces per nesting level */
      (void)fprintf(list_fd,fmt,"");
    }
    if(gsymt->internal_entry)
      (void)fprintf(list_fd,"%s entry ",gsymt->link.module->name);
    (void)fprintf(list_fd,"%s",gsymt->name);
  }



				/* Visit its unvisited children.  Note
				   that children of internal entry are
				   taken as those of its superior module.
				 */
  child_list = (gsymt->internal_entry?gsymt->link.module:gsymt)
		   ->link.child_list;

				/* If already visited, do not visit its
				   children, but give note to reader if it
				   has some. */
  if(PRUNE_CALLTREE && gsymt->visited) {
    if(print_call_tree && child_list != NULL)
      (void)fprintf(list_fd," (see above)");
  }
  else {
				/* Mark node as visited */
    gsymt->visited = TRUE;
				/* Record that containing module
				   is visited via this entry point*/
    if(gsymt->internal_entry)
      gsymt->link.module->visited_somewhere = TRUE;
    else
      gsymt->visited_somewhere = TRUE;

    ++level;			/* move to next level */
    while(child_list != NULL) {
      visit_child(child_list->child,level);
      child_list = child_list->next;
    }
  }
}

/*** visit_child_reflist

Same as visit_child, except it does a breadth-first search of the call
tree, and prints the results in the form of a who-calls-who list.

Contributed by: Gerome Emmanuel : Esial Troisieme annee
		Projet commun Esial / Ecole des mines
		INERIS
		E-mail: gerome@mines.u-nancy.fr
Date received: 20-APR-1993
Modified slightly to make it compatible as alternative to call-tree and
to make output format consistent.
***/

PRIVATE void
#if HAVE_STDC
visit_child_reflist(Gsymtab *gsymt)
#else /* K&R style */
visit_child_reflist(gsymt)
     Gsymtab *gsymt;
#endif /* HAVE_STDC */
{
  ChildList *child_list;

  child_list = (gsymt->internal_entry?gsymt->link.module:gsymt)
                   ->link.child_list;

                                /* If already visited, do not visit its
                                   children, but give note to reader if it
                                   has some. */
  if(!gsymt->visited) {
                                /* Mark node as visited */
    gsymt->visited = TRUE;
                                /* Record that containing module
                                   is visited via this entry point*/
    if(gsymt->internal_entry)
      gsymt->link.module->visited_somewhere = TRUE;
    else
      gsymt->visited_somewhere = TRUE;

    if(print_ref_list)		/* Print callees neatly if desired */
    {
#ifdef DYNAMIC_TABLES		/* tables will be mallocked at runtime */
      Gsymtab  **gsymlist;
#else
      Gsymtab  *gsymlist[GLOBSYMTABSZ];
#endif
      ChildList *child_list2;
      unsigned numcalls;

#ifdef DYNAMIC_TABLES
      if( (gsymlist=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL) {
	  oops_message(OOPS_FATAL,NO_LINE_NUM,NO_COL_NUM,
		       "Cannot malloc space for reference list");
      }
#endif

      (void)fprintf(list_fd,"\n%s calls:",gsymt->name);

      numcalls = 0;
      child_list2 = child_list;
      while(child_list2 != NULL)
	  {
	    gsymlist[numcalls++] = child_list2->child;
	    child_list2 = child_list2->next;
	  }

      if(numcalls == (unsigned)0)
	    (void)fprintf(list_fd," none");
      else {
	    (void)fprintf(list_fd,"\n");
	    print_modules(numcalls,gsymlist);
      }
#ifdef DYNAMIC_TABLES
      (void) cfree(gsymlist);
#endif
    }

    while(child_list != NULL) {
      visit_child_reflist(child_list->child);
      child_list = child_list->next;
    }
  }
}

/* visit_child_vcg:
  
  Same as visit_child_reflist except it provides output suitable for
  visualisation of the call graph, using the vcg graph visualisation
  program.  VCG is freely available from ftp.cs.uni-sb.de and
  elsewhere. It was written by G. Sander of the University of
  Saarland, Germany.

  Contributed by:  P.A.Rubini@cranfield.ac.uk
  Date: 3-APR-1995
*/

#ifdef VCG_SUPPORT
PRIVATE void
#if HAVE_STDC
visit_child_vcg(Gsymtab *gsymt, int level)
#else /* K&R style */
visit_child_vcg(gsymt,level)
     Gsymtab *gsymt;
     int level;
#endif /* HAVE_STDC */
{
  ArgListHeader *arglist;
  ChildList *child_list;

  child_list = (gsymt->internal_entry?gsymt->link.module:gsymt)
                   ->link.child_list;

                                /* If already visited, do not visit its
                                   children, but give note to reader if it
                                   has some. */
  if(!gsymt->visited) {
                                /* Mark node as visited */
    gsymt->visited = TRUE;
                                /* Record that containing module
                                   is visited via this entry point*/
    if(gsymt->internal_entry)
      gsymt->link.module->visited_somewhere = TRUE;
    else
      gsymt->visited_somewhere = TRUE;

    if(print_vcg_list)		/* Print callees neatly if desired */
    {
#ifdef DYNAMIC_TABLES		/* tables will be mallocked at runtime */
      Gsymtab  **gsymlist;
#else
      Gsymtab  *gsymlist[GLOBSYMTABSZ];
#endif
      ChildList *child_list2;
      int j;
      unsigned numcalls;

#ifdef DYNAMIC_TABLES
      if( (gsymlist=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL) {
	  oops_message(OOPS_FATAL,NO_LINE_NUM,NO_COL_NUM,
		       "Cannot malloc space for reference list");
      }
#endif

    numcalls = 0;
    child_list2 = child_list;
    while(child_list2 != NULL)
	  {
	    gsymlist[numcalls++] = child_list2->child;
	    child_list2 = child_list2->next;
	  }

    arglist = gsymt->info.arglist;
    while(arglist != NULL) {
      if ( arglist->is_defn ) {

         (void)fprintf(vcg_fd,"\ngraph: {\ntitle:\"[%s]\"\n",gsymt->name);
         (void)fprintf(vcg_fd,
	      "node: { title: \"%s\" label: \"%s \\n (%s)\" info1:\"%d\" }\n",
                    gsymt->name,gsymt->name,
                    arglist->filename,
                    level );


	  if(numcalls != (unsigned)0) {
		for (j=0;j<numcalls;j++){
		   arglist = gsymlist[j]->info.arglist;
		   while(arglist != NULL) {
		     if ( arglist->is_defn ) {
			(void)fprintf(vcg_fd,
		 "edge: { sourcename: \"%s\" targetname: \"%s\" class:%d} \n",
			    gsymt->name,gsymlist[j]->name,
                            level );
			break ;
		     }
                     arglist = arglist->next;
		   }
		}
	  }
          break;
      }
      arglist = arglist->next;
    }
#ifdef DYNAMIC_TABLES
      (void) cfree(gsymlist);
#endif

    ++level;			/* move to next level */

/*  while(child_list != NULL) {
      visit_child_vcg(child_list->child,level);
      child_list = child_list->next;
    } */

    for (j=0;j<numcalls;j++){
       arglist = gsymlist[j]->info.arglist;
       while(arglist != NULL) {
          if ( arglist->is_defn ) {
             visit_child_vcg(gsymlist[j],level);
             break ;
          }
          arglist = arglist->next;
       }
    }
    (void)fprintf(vcg_fd,"}\n");
    }
  }
}

#endif /* VCG_SUPPORT */


PRIVATE void
print_crossrefs(VOID)
{
#ifdef DYNAMIC_TABLES		/* tables will be mallocked at runtime */
      Gsymtab  **gsymlist, **modulelist;
#else
  Gsymtab  *gsymlist[GLOBSYMTABSZ], *modulelist[GLOBSYMTABSZ];
#endif
  ArgListHeader *args;
  int  i,numentries;
  unsigned numcalls;

#ifdef DYNAMIC_TABLES
      if( (gsymlist=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL ||
	 (modulelist=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL) {
	  oops_message(OOPS_FATAL,NO_LINE_NUM,NO_COL_NUM,
		       "Cannot malloc space for crossref list");
      }
#endif

				/* Gather up all relevant subprograms */
  for(i=0,numentries=0; i<glob_symtab_top; i++) {
    if(storage_class_of(glob_symtab[i].type) == class_SUBPROGRAM
       && (glob_symtab[i].visited || !glob_symtab[i].library_module)) {
      gsymlist[numentries++] = &glob_symtab[i];
    }
  }

  if(numentries > 0) {
    (void)fprintf(list_fd,"\n\n        Cross-reference list:\n");

				/* Sort the subprograms */
    sort_gsymbols(gsymlist,numentries);

				/* Print their callers */
    for(i=0; i<numentries; i++) {
      (void)fprintf(list_fd,"\n");
      if(gsymlist[i]->internal_entry)
	(void)fprintf(list_fd,"%s entry ",gsymlist[i]->link.module->name);
      (void)fprintf(list_fd,"%s",gsymlist[i]->name);

      numcalls=0;
      args = gsymlist[i]->info.arglist;
      while(args != NULL) {		/* Gather up callers */
	if(!args->is_defn) {
				/* (eliminate duplicates) */
	  if(numcalls==(unsigned) 0 || args->module != modulelist[numcalls-1])
	    modulelist[numcalls++] = args->module;
	}
	args = args->next;
      }

      if(numcalls == (unsigned) 0)
	(void)fprintf(list_fd," not called");
      else {
	(void)fprintf(list_fd," called by:\n");
	sort_gsymbols(modulelist,(int)numcalls); /* Sort the callers */
	print_modules(numcalls,modulelist);
      }
    }
    (void)fprintf(list_fd,"\n");
  }
#ifdef DYNAMIC_TABLES
      (void) cfree(gsymlist);
      (void) cfree(modulelist);
#endif
}


	/* Topological sort of the call tree.  Based closely on algorithm
	   on page 314 of Horowitz and Sahni, Fundamentals of Data
	   Structures.  Returns TRUE if successful, FALSE if failed
	   due to a cycle being detected.
	 */

PRIVATE int
#if HAVE_STDC
toposort(Gsymtab *gsymt, int nsym)
#else /* K&R style */
toposort(gsymt,nsym)
     Gsymtab gsymt[];
     int nsym;
#endif /* HAVE_STDC */
{
  int i,num_nodes, node_count;
  ChildList *child_list;
  Gsymtab *child_module;	/* Called module's top entry point */
#ifdef DYNAMIC_TABLES		/* tables will be mallocked at runtime */
  int *parent_count;
  Gsymtab **node_list;
#else
  int parent_count[GLOBSYMTABSZ];
  Gsymtab *node_list[GLOBSYMTABSZ];
#endif

#ifdef DYNAMIC_TABLES
      if( (parent_count=(int *)calloc(glob_symtab_top,sizeof(int)))
	 == (int *)NULL ||
	 (node_list=(Gsymtab **)calloc(glob_symtab_top,sizeof(Gsymtab *)))
	 == (Gsymtab **)NULL) {
	  oops_message(OOPS_FATAL,NO_LINE_NUM,NO_COL_NUM,
		       "Cannot malloc space for module sort");
      }
#endif
			/* Initialize array of links/counts */
  for(i=0; i<nsym; i++)
    parent_count[i] = 0;	/* In-order of module as node */

			/* Traverse child lists, incrementing their
			   parent counts.
			 */
  for(i=0,num_nodes=0; i<nsym; i++) {
    if(gsymt[i].visited_somewhere) { /* skip entry pts and com blocks */
      ++num_nodes;
      child_list = gsymt[i].link.child_list;
      while(child_list != NULL) {
				/* If child is an internal entry, substitute
				   top entry point of its subprogram unit. */
	if( (child_module=child_list->child)->internal_entry )
	  child_module = child_module->link.module;
	++parent_count[child_module - gsymt]; /* index into table */
	child_list = child_list->next;
      }
    }
  }

  {				/* Start of the sort */
    int top=0;
    int j,k;

    for(i=0; i<nsym; i++) {
      if(gsymt[i].visited_somewhere && parent_count[i] == 0) {
	parent_count[i] = top;	/* Link now-parentless module into stack */
	top = i+1;
      }
    }
    for(i=0,node_count=0; i<num_nodes; i++) {
      if(top == 0) {
	if(print_topo_sort) {
	  (void)fprintf(list_fd,"\nCall tree has a cycle");
	  print_cycle_nodes(gsymt,nsym,node_list,node_count,parent_count);
	}
	break;
      }
      j = top-1;
      top = parent_count[j];	/* Recover the link */

				/* Print the next module */
      if(print_topo_sort) {
	node_list[node_count++] = &gsymt[j];
	parent_count[j] = -1;
      }
			/* Decrease parent count of its children */
      child_list = gsymt[j].link.child_list;
      while(child_list != NULL) {
	if( (child_module=child_list->child)->internal_entry )
	  child_module = child_module->link.module;
	k = child_module - gsymt;
	if(--parent_count[k] == 0) { /* Now parentless? Stack it*/
	  parent_count[k] = top;
	  top = k+1;
	}
	child_list = child_list->next;
      }
    }
  }/*end sort*/

  if(print_topo_sort && node_count > 0) {
    (void)fprintf(list_fd,"\nList of called modules in prerequisite order:\n");
    print_modules(node_count,node_list);
    (void)fprintf(list_fd,"\n");
  }

#ifdef DYNAMIC_TABLES
  (void) cfree(parent_count);
  (void) cfree(node_list);
#endif

  return (node_count==num_nodes);	/* Success = TRUE */
}

		/* Traces back to find nodes not listed in topological
		   sort.  They are the cycle nodes and their descendants.
		 */
PRIVATE void
#if HAVE_STDC
print_cycle_nodes(Gsymtab *gsymt, int nsym, Gsymtab **node_list, int node_count, int *parent_count)
#else /* K&R style */
print_cycle_nodes(gsymt,nsym,node_list,node_count,parent_count)
     Gsymtab gsymt[];
     int nsym;
     Gsymtab *node_list[];
     int node_count;
     int parent_count[];
#endif /* HAVE_STDC */
{
  int i;
  int k=node_count;
  for(i=0; i<nsym; i++) {
    if(gsymt[i].visited_somewhere) {
      if(parent_count[i] != -1)	/* Not tagged */
	node_list[k++] = &gsymt[i];
    }
  }
  if(k > node_count)
    (void)fprintf(list_fd," containing some of the following modules:\n");
  print_modules(k-node_count,node_list+node_count);
}


				/* Insertion sort of child list.
				   Also removes duplicates which
				   can be introduced via multiple
				   defns or via project files. */
PRIVATE ChildList *
#if HAVE_STDC
sort_child_list(ChildList *child_list)
#else /* K&R style */
sort_child_list(child_list)
     ChildList *child_list;
#endif /* HAVE_STDC */
{
 if( SORT_CALLTREE ) {
  ChildList *front,*prev,*next,*cl=child_list;
  Gsymtab *temp;
  prev = NULL;
  while(cl != NULL) {
			/* Scan thru list for lexicographically lowest name */
    front=cl;
    for(next=cl->next; next != NULL; next = next->next) {
      if(strcmp(front->child->name,next->child->name) > 0) {
	front = next;
      }
    }
			/* Swap child pointers so front is first */
    if(front != cl) {
      temp = front->child;
      front->child = cl->child;
      cl->child = temp;
    }
			/* If duplicate, remove from list */
    if(prev != NULL && prev->child == cl->child)
      prev->next = cl->next;
    else
      prev = cl;
    cl = cl->next;
  }
  return child_list;

 }
 else  /* put children in program order, i.e. reverse the list */
 {
  ChildList *curr,*next,*temp;
  if(child_list == NULL)
    return child_list;
  curr = child_list;
  next = curr->next;
  while(next != NULL) {
    temp = next->next;
    next->next = curr;		/* switch the pointers to point in reverse */
    curr = next;
    next = temp;
  }
  child_list->next = NULL;	/* former head is now tail */
  return curr;			/* and curr now points to new head */
 }
}



PRIVATE void
#if HAVE_STDC
sort_gsymbols (Gsymtab **glist, int n)   /* bubble sort, same as sort_symbols */
#else /* K&R style */
sort_gsymbols ( glist,n )   /* bubble sort, same as sort_symbols */
	Gsymtab *glist[];
	int n;
#endif /* HAVE_STDC */
{
	int i,j,swaps;

	for (i=0; i<n; i++ ){
	    swaps = 0;
	    for  (j=n-1; j>=i+1; j--){
		if ((strcmp (glist[j-1]->name, glist[j]->name)) >0) {
		    swap_gsymptrs(&glist[j-1], &glist[j] );
		    swaps++;
		}
	    }
	    if (swaps == 0) break;
	}


}

PRIVATE void
#if HAVE_STDC
swap_gsymptrs (Gsymtab **x_ptr, Gsymtab **y_ptr)    /* swap pointers */
#else /* K&R style */
swap_gsymptrs (x_ptr, y_ptr)    /* swap pointers */
	Gsymtab **x_ptr,**y_ptr;
#endif /* HAVE_STDC */
{
	Gsymtab *temp = *x_ptr;
	*x_ptr = *y_ptr;
	*y_ptr = temp;
}

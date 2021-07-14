// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/interval_tree.c - interval tree for mapping->i_mmap
 *
 * Copyright (C) 2012, Michel Lespinasse <walken@google.com>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rmap.h>
#include <linux/interval_tree_generic.h>

static inline unsigned long vma_start_pgoff(struct vm_area_struct *v)   /*  */
{
	return v->vm_pgoff;
}

static inline unsigned long vma_last_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff + vma_pages(v)/* 页数 */ - 1;
}

INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.rb,
		     unsigned long, shared.rb_subtree_last,
		     vma_start_pgoff, vma_last_pgoff,, vma_interval_tree);/**/

/* INTERVAL_TREE_DEFINE 展开如下 */
#if __RTOAX_________INTERVAL_TREE_DEFINE
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Interval Trees
  (C) 2012  Michel Lespinasse <walken@google.com>


  include/linux/interval_tree_generic.h
*/

#include <linux/rbtree_augmented.h>

/**
 *  进行了 rbtree 的封装
 */

/*
 * Template for implementing interval trees
 *
 * struct vm_area_struct:   struct type of the interval tree nodes
 * shared.rb:       name of struct rb_node field within struct vm_area_struct
 * unsigned long:     type of the interval endpoints
 * shared.rb_subtree_last:  name of unsigned long field within struct vm_area_struct holding last-in-subtree
 * vma_start_pgoff(n): start endpoint of struct vm_area_struct node n
 * vma_last_pgoff(n):  last endpoint of struct vm_area_struct node n
 * :   'static' or empty
 * vma_interval_tree:   prefix to use for the inline tree definitions
 *
 * Note - before using this, please consider if generic version
 * (interval_tree.h) would work for you...
 */
/* Callbacks for augmented rbtree insert and remove */			      
									            

/*
 * Template for declaring augmented rbtree callbacks (generic case)
 *
 * static:    'static' or empty
 * vma_interval_tree_augment:      name of the rb_augment_callbacks structure
 * struct vm_area_struct:    struct type of the tree nodes
 * shared.rb:     name of struct rb_node field within struct vm_area_struct
 * shared.rb_subtree_last: name of field within struct vm_area_struct holding data for subtree
 * vma_last_pgoff:   name of function that recomputes the shared.rb_subtree_last data
 */

// #define RB_DECLARE_CALLBACKS(static, vma_interval_tree_augment,				
			     // struct vm_area_struct, shared.rb, shared.rb_subtree_last, vma_last_pgoff)	
static inline void							
vma_interval_tree_augment_propagate(struct rb_node *rb, struct rb_node *stop)		
{									
	while (rb != stop) {						
		struct vm_area_struct *node = rb_entry(rb, struct vm_area_struct, shared.rb);	
		if (vma_last_pgoff(node, true))				
			break;						
		rb = rb_parent(&node->shared.rb);				
	}								
}									
static inline void							
vma_interval_tree_augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)		
{									
	struct vm_area_struct *old = rb_entry(rb_old, struct vm_area_struct, shared.rb);		
	struct vm_area_struct *new = rb_entry(rb_new, struct vm_area_struct, shared.rb);		
	new->shared.rb_subtree_last = old->shared.rb_subtree_last;				
}									
static void								
vma_interval_tree_augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new)	
{									
	struct vm_area_struct *old = rb_entry(rb_old, struct vm_area_struct, shared.rb);		
	struct vm_area_struct *new = rb_entry(rb_new, struct vm_area_struct, shared.rb);		
	new->shared.rb_subtree_last = old->shared.rb_subtree_last;				
	vma_last_pgoff(old, false);						
}									
static const struct rb_augment_callbacks vma_interval_tree_augment = {			
	.propagate = vma_interval_tree_augment_propagate,				
	.copy = vma_interval_tree_augment_copy,					
	.rotate = vma_interval_tree_augment_rotate					
};

/*
 * Template for declaring augmented rbtree callbacks,
 * computing shared.rb_subtree_last scalar as max(vma_last_pgoff(node)) for all subtree nodes.
 *
 * static:    'static' or empty
 * vma_interval_tree_augment:      name of the rb_augment_callbacks structure
 * struct vm_area_struct:    struct type of the tree nodes
 * shared.rb:     name of struct rb_node field within struct vm_area_struct
 * unsigned long:      type of the shared.rb_subtree_last field
 * shared.rb_subtree_last: name of unsigned long field within struct vm_area_struct holding data for subtree
 * vma_last_pgoff:   name of function that returns the per-node unsigned long scalar
 */

// RB_DECLARE_CALLBACKS_MAX(static, vma_interval_tree_augment,			      
			 // struct vm_area_struct, shared.rb, unsigned long, shared.rb_subtree_last, vma_last_pgoff)	

// #define RB_DECLARE_CALLBACKS_MAX(static, vma_interval_tree_augment, struct vm_area_struct, shared.rb,	      
				 // unsigned long, shared.rb_subtree_last, vma_last_pgoff)	      
static inline bool vma_interval_tree_augment_compute_max(struct vm_area_struct *node, bool exit)	      
{									      
	struct vm_area_struct *child;						      
	unsigned long max = vma_last_pgoff(node);					      
	if (node->shared.rb.rb_left) {					      
		child = rb_entry(node->shared.rb.rb_left, struct vm_area_struct, shared.rb);   
		if (child->shared.rb_subtree_last > max)				      
			max = child->shared.rb_subtree_last;			      
	}								      
	if (node->shared.rb.rb_right) {					      
		child = rb_entry(node->shared.rb.rb_right, struct vm_area_struct, shared.rb);  
		if (child->shared.rb_subtree_last > max)				      
			max = child->shared.rb_subtree_last;			      
	}								      
	if (exit && node->shared.rb_subtree_last == max)				      
		return true;						      
	node->shared.rb_subtree_last = max;					      
	return false;							      
}									      
//RB_DECLARE_CALLBACKS(static, vma_interval_tree_augment,					      
//		     struct vm_area_struct, shared.rb, shared.rb_subtree_last, vma_interval_tree_augment_compute_max)

             
/* Insert / remove interval nodes from the tree */			      
									      
 void vma_interval_tree_insert(struct vm_area_struct *node,			      
				  struct rb_root_cached *root)	 	      
{									      
	struct rb_node **link = &root->rb_root.rb_node, *rb_parent = NULL;    
	unsigned long start = vma_start_pgoff(node), last = vma_last_pgoff(node);		      
	struct vm_area_struct *parent;						      
	bool leftmost = true;						      
									      
	while (*link) {							      
		rb_parent = *link;					      
		parent = rb_entry(rb_parent, struct vm_area_struct, shared.rb);		      
		if (parent->shared.rb_subtree_last < last)				      
			parent->shared.rb_subtree_last = last;			      
		if (start < vma_start_pgoff(parent))				      
			link = &parent->shared.rb.rb_left;			      
		else {							      
			link = &parent->shared.rb.rb_right;			      
			leftmost = false;				      
		}							      
	}								      
									      
	node->shared.rb_subtree_last = last;						      
	rb_link_node(&node->shared.rb, rb_parent, link);			      
	rb_insert_augmented_cached(&node->shared.rb, root,			      
				   leftmost, &vma_interval_tree_augment);	      
}									      
									      
 void vma_interval_tree_remove(struct vm_area_struct *node,			      
				  struct rb_root_cached *root)		      
{									      
	rb_erase_augmented_cached(&node->shared.rb, root, &vma_interval_tree_augment);  
}									      
									      
/*									      
 * Iterate over intervals intersecting [start;last]			      
 *									      
 * Note that a node's interval intersects [start;last] iff:		      
 *   Cond1: vma_start_pgoff(node) <= last					      
 * and									      
 *   Cond2: start <= vma_last_pgoff(node)					      
 */									      
									      
static struct vm_area_struct *							      
vma_interval_tree_subtree_search(struct vm_area_struct *node, unsigned long start, unsigned long last)	      
{									      
	while (true) {							      
		/*							      
		 * Loop invariant: start <= node->shared.rb_subtree_last		      
		 * (Cond2 is satisfied by one of the subtree nodes)	      
		 */							      
		if (node->shared.rb.rb_left) {				      
			struct vm_area_struct *left = rb_entry(node->shared.rb.rb_left,	      
						  struct vm_area_struct, shared.rb);	      
			if (start <= left->shared.rb_subtree_last) {			      
				/*					      
				 * Some nodes in left subtree satisfy Cond2.  
				 * Iterate to find the leftmost such node N.  
				 * If it also satisfies Cond1, that's the     
				 * match we are looking for. Otherwise, there 
				 * is no matching interval as nodes to the    
				 * right of N can't satisfy Cond1 either.     
				 */					      
				node = left;				      
				continue;				      
			}						      
		}							      
		if (vma_start_pgoff(node) <= last) {		/* Cond1 */	      
			if (start <= vma_last_pgoff(node))	/* Cond2 */	      
				return node;	/* node is leftmost match */  
			if (node->shared.rb.rb_right) {			      
				node = rb_entry(node->shared.rb.rb_right,	      
						struct vm_area_struct, shared.rb);	      
				if (start <= node->shared.rb_subtree_last)		      
					continue;			      
			}						      
		}							      
		return NULL;	/* No match */				      
	}								      
}									      
									      
 struct vm_area_struct *							      
vma_interval_tree_iter_first(struct rb_root_cached *root,			      
			unsigned long start, unsigned long last)			      
{									      
	struct vm_area_struct *node, *leftmost;					      
									      
	if (!root->rb_root.rb_node)					      
		return NULL;						      
									      
	/*								      
	 * Fastpath range intersection/overlap between A: [a0, a1] and	      
	 * B: [b0, b1] is given by:					      
	 *								      
	 *         a0 <= b1 && b0 <= a1					      
	 *								      
	 *  ... where A holds the lock range and B holds the smallest	      
	 * 'start' and largest 'last' in the tree. For the later, we	      
	 * rely on the root node, which by augmented interval tree	      
	 * property, holds the largest value in its last-in-subtree.	      
	 * This allows mitigating some of the tree walk overhead for	      
	 * for non-intersecting ranges, maintained and consulted in O(1).     
	 */								      
	node = rb_entry(root->rb_root.rb_node, struct vm_area_struct, shared.rb);		      
	if (node->shared.rb_subtree_last < start)					      
		return NULL;						      
									      
	leftmost = rb_entry(root->rb_leftmost, struct vm_area_struct, shared.rb);		      
	if (vma_start_pgoff(leftmost) > last)					      
		return NULL;						      
									      
	return vma_interval_tree_subtree_search(node, start, last);		      
}									      
									      
 struct vm_area_struct *							      
vma_interval_tree_iter_next(struct vm_area_struct *node, unsigned long start, unsigned long last)	      
{									      
	struct rb_node *rb = node->shared.rb.rb_right, *prev;		      
									      
	while (true) {							      
		/*							      
		 * Loop invariants:					      
		 *   Cond1: vma_start_pgoff(node) <= last			      
		 *   rb == node->shared.rb.rb_right				      
		 *							      
		 * First, search right subtree if suitable		      
		 */							      
		if (rb) {						      
			struct vm_area_struct *right = rb_entry(rb, struct vm_area_struct, shared.rb);	      
			if (start <= right->shared.rb_subtree_last)			      
				return vma_interval_tree_subtree_search(right,     
								start, last); 
		}							      
									      
		/* Move up the tree until we come from a node's left child */ 
		do {							      
			rb = rb_parent(&node->shared.rb);			      
			if (!rb)					      
				return NULL;				      
			prev = &node->shared.rb;				      
			node = rb_entry(rb, struct vm_area_struct, shared.rb);		      
			rb = node->shared.rb.rb_right;			      
		} while (prev == rb);					      
									      
		/* Check if the node intersects [start;last] */		      
		if (last < vma_start_pgoff(node))		/* !Cond1 */	      
			return NULL;					      
		else if (start <= vma_last_pgoff(node))		/* Cond2 */	      
			return node;					      
	}								      
}

#endif //__RTOAX_________INTERVAL_TREE_DEFINE

/* Insert node immediately after prev in the interval tree */
void vma_interval_tree_insert_after(struct vm_area_struct *node,
				    struct vm_area_struct *prev,
				    struct rb_root_cached *root)
{
	struct rb_node **link;
	struct vm_area_struct *parent;
	unsigned long last = vma_last_pgoff(node);

	VM_BUG_ON_VMA(vma_start_pgoff(node) != vma_start_pgoff(prev), node);

	if (!prev->shared.rb.rb_right) {
		parent = prev;
		link = &prev->shared.rb.rb_right;
	} else {
		parent = rb_entry(prev->shared.rb.rb_right,
				  struct vm_area_struct, shared.rb);
		if (parent->shared.rb_subtree_last < last)
			parent->shared.rb_subtree_last = last;
		while (parent->shared.rb.rb_left) {
			parent = rb_entry(parent->shared.rb.rb_left,
				struct vm_area_struct, shared.rb);
			if (parent->shared.rb_subtree_last < last)
				parent->shared.rb_subtree_last = last;
		}
		link = &parent->shared.rb.rb_left;
	}

	node->shared.rb_subtree_last = last;
	rb_link_node(&node->shared.rb, &parent->shared.rb, link);
	rb_insert_augmented(&node->shared.rb, &root->rb_root,
			    &vma_interval_tree_augment);
}

static inline unsigned long avc_start_pgoff(struct anon_vma_chain *avc) /*  */
{
	return vma_start_pgoff(avc->vma);
}

static inline unsigned long avc_last_pgoff(struct anon_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static inline, __anon_vma_interval_tree);

/* INTERVAL_TREE_DEFINE 展开如下 */
#if __RTOAX_________INTERVAL_TREE_DEFINE
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Interval Trees
  (C) 2012  Michel Lespinasse <walken@google.com>


  include/linux/interval_tree_generic.h
*/

#include <linux/rbtree_augmented.h>

/**
 *  进行了 rbtree 的封装
 */

/*
 * Template for implementing interval trees
 *
 * struct anon_vma_chain:   struct type of the interval tree nodes
 * rb:       name of struct rb_node field within struct anon_vma_chain
 * unsigned long:     type of the interval endpoints
 * rb_subtree_last:  name of unsigned long field within struct anon_vma_chain holding last-in-subtree
 * avc_start_pgoff(n): start endpoint of struct anon_vma_chain node n
 * avc_last_pgoff(n):  last endpoint of struct anon_vma_chain node n
 * static inline:   'static' or empty
 * __anon_vma_interval_tree:   prefix to use for the inline tree definitions
 *
 * Note - before using this, please consider if generic version
 * (interval_tree.h) would work for you...
 */				      
/* Callbacks for augmented rbtree insert and remove */			      
									      
// RB_DECLARE_CALLBACKS_MAX(static, __anon_vma_interval_tree_augment,			      
			 // struct anon_vma_chain, rb, unsigned long, rb_subtree_last, avc_last_pgoff)	      


/*
 * Template for declaring augmented rbtree callbacks,
 * computing rb_subtree_last scalar as max(avc_last_pgoff(node)) for all subtree nodes.
 *
 * static:    'static' or empty
 * __anon_vma_interval_tree_augment:      name of the rb_augment_callbacks structure
 * struct anon_vma_chain:    struct type of the tree nodes
 * rb:     name of struct rb_node field within struct anon_vma_chain
 * unsigned long:      type of the rb_subtree_last field
 * rb_subtree_last: name of unsigned long field within struct anon_vma_chain holding data for subtree
 * avc_last_pgoff:   name of function that returns the per-node unsigned long scalar
 */

// #define RB_DECLARE_CALLBACKS_MAX(static, __anon_vma_interval_tree_augment, struct anon_vma_chain, rb,	      
				 // unsigned long, rb_subtree_last, avc_last_pgoff)	      
static inline bool __anon_vma_interval_tree_augment_compute_max(struct anon_vma_chain *node, bool exit)	      
{									      
	struct anon_vma_chain *child;						      
	unsigned long max = avc_last_pgoff(node);					      
	if (node->rb.rb_left) {					      
		child = rb_entry(node->rb.rb_left, struct anon_vma_chain, rb);   
		if (child->rb_subtree_last > max)				      
			max = child->rb_subtree_last;			      
	}								      
	if (node->rb.rb_right) {					      
		child = rb_entry(node->rb.rb_right, struct anon_vma_chain, rb);  
		if (child->rb_subtree_last > max)				      
			max = child->rb_subtree_last;			      
	}								      
	if (exit && node->rb_subtree_last == max)				      
		return true;						      
	node->rb_subtree_last = max;					      
	return false;							      
}									      
// RB_DECLARE_CALLBACKS(static, __anon_vma_interval_tree_augment,					      
		     // struct anon_vma_chain, rb, rb_subtree_last, __anon_vma_interval_tree_augment_compute_max)
/*
 * Template for declaring augmented rbtree callbacks (generic case)
 *
 * static:    'static' or empty
 * __anon_vma_interval_tree_augment:      name of the rb_augment_callbacks structure
 * struct anon_vma_chain:    struct type of the tree nodes
 * rb:     name of struct rb_node field within struct anon_vma_chain
 * rb_subtree_last: name of field within struct anon_vma_chain holding data for subtree
 * avc_last_pgoff:   name of function that recomputes the rb_subtree_last data
 */

//#define RB_DECLARE_CALLBACKS(static, __anon_vma_interval_tree_augment,				
//			     struct anon_vma_chain, rb, rb_subtree_last, avc_last_pgoff)	
static inline void							
__anon_vma_interval_tree_augment_propagate(struct rb_node *rb, struct rb_node *stop)		
{									
	while (rb != stop) {						
		struct anon_vma_chain *node = rb_entry(rb, struct anon_vma_chain, rb);	
		if (avc_last_pgoff(node, true))				
			break;						
		rb = rb_parent(&node->rb);				
	}								
}									
static inline void							
__anon_vma_interval_tree_augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)		
{									
	struct anon_vma_chain *old = rb_entry(rb_old, struct anon_vma_chain, rb);		
	struct anon_vma_chain *new = rb_entry(rb_new, struct anon_vma_chain, rb);		
	new->rb_subtree_last = old->rb_subtree_last;				
}									
static void								
__anon_vma_interval_tree_augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new)	
{									
	struct anon_vma_chain *old = rb_entry(rb_old, struct anon_vma_chain, rb);		
	struct anon_vma_chain *new = rb_entry(rb_new, struct anon_vma_chain, rb);		
	new->rb_subtree_last = old->rb_subtree_last;				
	avc_last_pgoff(old, false);						
}									
static const struct rb_augment_callbacks __anon_vma_interval_tree_augment = {			
	.propagate = __anon_vma_interval_tree_augment_propagate,				
	.copy = __anon_vma_interval_tree_augment_copy,					
	.rotate = __anon_vma_interval_tree_augment_rotate					
};

             
/* Insert / remove interval nodes from the tree */			      
									      
static inline void __anon_vma_interval_tree_insert(struct anon_vma_chain *node,			      
				  struct rb_root_cached *root)	 	      
{									      
	struct rb_node **link = &root->rb_root.rb_node, *rb_parent = NULL;   

    /* 计算当前 AVC 对应的 VMA 的  */
	unsigned long start = avc_start_pgoff(node), last = avc_last_pgoff(node);		      
	struct anon_vma_chain *parent;						      
	bool leftmost = true;						      
									      
	while (*link) {							      
		rb_parent = *link;					      
		parent = rb_entry(rb_parent, struct anon_vma_chain, rb);		      
		if (parent->rb_subtree_last < last)				      
			parent->rb_subtree_last = last;			      
		if (start < avc_start_pgoff(parent))				      
			link = &parent->rb.rb_left;			      
		else {							      
			link = &parent->rb.rb_right;			      
			leftmost = false;				      
		}							      
	}								      
									      
	node->rb_subtree_last = last;	

    /* 插入红黑树 */
	rb_link_node(&node->rb, rb_parent, link);			      
	rb_insert_augmented_cached(&node->rb, root,			      
				   leftmost, &__anon_vma_interval_tree_augment);	      
}									      
									      
static inline void __anon_vma_interval_tree_remove(struct anon_vma_chain *node,			      
				  struct rb_root_cached *root)		      
{									      
	rb_erase_augmented_cached(&node->rb, root, &__anon_vma_interval_tree_augment);  
}									      
									      
/*									      
 * Iterate over intervals intersecting [start;last]			      
 *									      
 * Note that a node's interval intersects [start;last] iff:		      
 *   Cond1: avc_start_pgoff(node) <= last					      
 * and									      
 *   Cond2: start <= avc_last_pgoff(node)					      
 */									      
									      
static struct anon_vma_chain *							      
__anon_vma_interval_tree_subtree_search(struct anon_vma_chain *node, unsigned long start, unsigned long last)	      
{									      
	while (true) {							      
		/*							      
		 * Loop invariant: start <= node->rb_subtree_last		      
		 * (Cond2 is satisfied by one of the subtree nodes)	      
		 */							      
		if (node->rb.rb_left) {				      
			struct anon_vma_chain *left = rb_entry(node->rb.rb_left,	      
						  struct anon_vma_chain, rb);	      
			if (start <= left->rb_subtree_last) {			      
				/*					      
				 * Some nodes in left subtree satisfy Cond2.  
				 * Iterate to find the leftmost such node N.  
				 * If it also satisfies Cond1, that's the     
				 * match we are looking for. Otherwise, there 
				 * is no matching interval as nodes to the    
				 * right of N can't satisfy Cond1 either.     
				 */					      
				node = left;				      
				continue;				      
			}						      
		}							      
		if (avc_start_pgoff(node) <= last) {		/* Cond1 */	      
			if (start <= avc_last_pgoff(node))	/* Cond2 */	      
				return node;	/* node is leftmost match */  
			if (node->rb.rb_right) {			      
				node = rb_entry(node->rb.rb_right,	      
						struct anon_vma_chain, rb);	      
				if (start <= node->rb_subtree_last)		      
					continue;			      
			}						      
		}							      
		return NULL;	/* No match */				      
	}								      
}									      
									      
static inline struct anon_vma_chain *							      
__anon_vma_interval_tree_iter_first(struct rb_root_cached *root,			      
			unsigned long start, unsigned long last)			      
{									      
	struct anon_vma_chain *node, *leftmost;					      
									      
	if (!root->rb_root.rb_node)					      
		return NULL;						      
									      
	/*								      
	 * Fastpath range intersection/overlap between A: [a0, a1] and	      
	 * B: [b0, b1] is given by:					      
	 *								      
	 *         a0 <= b1 && b0 <= a1					      
	 *								      
	 *  ... where A holds the lock range and B holds the smallest	      
	 * 'start' and largest 'last' in the tree. For the later, we	      
	 * rely on the root node, which by augmented interval tree	      
	 * property, holds the largest value in its last-in-subtree.	      
	 * This allows mitigating some of the tree walk overhead for	      
	 * for non-intersecting ranges, maintained and consulted in O(1).     
	 */								      
	node = rb_entry(root->rb_root.rb_node, struct anon_vma_chain, rb);		      
	if (node->rb_subtree_last < start)					      
		return NULL;						      
									      
	leftmost = rb_entry(root->rb_leftmost, struct anon_vma_chain, rb);		      
	if (avc_start_pgoff(leftmost) > last)					      
		return NULL;						      
									      
	return __anon_vma_interval_tree_subtree_search(node, start, last);		      
}									      
									      
static inline struct anon_vma_chain *							      
__anon_vma_interval_tree_iter_next(struct anon_vma_chain *node, unsigned long start, unsigned long last)	      
{									      
	struct rb_node *rb = node->rb.rb_right, *prev;		      
									      
	while (true) {							      
		/*							      
		 * Loop invariants:					      
		 *   Cond1: avc_start_pgoff(node) <= last			      
		 *   rb == node->rb.rb_right				      
		 *							      
		 * First, search right subtree if suitable		      
		 */							      
		if (rb) {						      
			struct anon_vma_chain *right = rb_entry(rb, struct anon_vma_chain, rb);	      
			if (start <= right->rb_subtree_last)			      
				return __anon_vma_interval_tree_subtree_search(right,     
								start, last); 
		}							      
									      
		/* Move up the tree until we come from a node's left child */ 
		do {							      
			rb = rb_parent(&node->rb);			      
			if (!rb)					      
				return NULL;				      
			prev = &node->rb;				      
			node = rb_entry(rb, struct anon_vma_chain, rb);		      
			rb = node->rb.rb_right;			      
		} while (prev == rb);					      
									      
		/* Check if the node intersects [start;last] */		      
		if (last < avc_start_pgoff(node))		/* !Cond1 */	      
			return NULL;					      
		else if (start <= avc_last_pgoff(node))		/* Cond2 */	      
			return node;					      
	}								      
}


#endif  //__RTOAX_________INTERVAL_TREE_DEFINE


/* 将 AVC 插入 AV 红黑树中 */
void anon_vma_interval_tree_insert(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
#ifdef CONFIG_DEBUG_VM_RB
	node->cached_vma_start = avc_start_pgoff(node);
	node->cached_vma_last = avc_last_pgoff(node);
#endif
    /*  */
	__anon_vma_interval_tree_insert(node, root);
}

void anon_vma_interval_tree_remove(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
	__anon_vma_interval_tree_remove(node, root);
}

struct anon_vma_chain *
anon_vma_interval_tree_iter_first(struct rb_root_cached *root,
				  unsigned long first, unsigned long last)
{
	return __anon_vma_interval_tree_iter_first(root, first, last);
}

struct anon_vma_chain *
anon_vma_interval_tree_iter_next(struct anon_vma_chain *node,
				 unsigned long first, unsigned long last)
{
	return __anon_vma_interval_tree_iter_next(node, first, last);
}

#ifdef CONFIG_DEBUG_VM_RB
void anon_vma_interval_tree_verify(struct anon_vma_chain *node)
{
	WARN_ON_ONCE(node->cached_vma_start != avc_start_pgoff(node));
	WARN_ON_ONCE(node->cached_vma_last != avc_last_pgoff(node));
}
#endif

/*
** Write ahead logging implementation copyright Chris Mason 2000
**
**
** The background commits make this code very interelated, and 
** overly complex.  I need to rethink things a bit....The major players:
**
** journal_begin -- call with the number of blocks you expect to log.  
**                  If the current transaction is too
** 		    old, it will block until the current transaction is 
** 		    finished, and then start a new one.
**		    Usually, your transaction will get joined in with 
**                  previous ones for speed.
**
** journal_join  -- same as journal_begin, but won't block on the current 
**                  transaction regardless of age.  Don't ever call
**                  this.  Ever.  There are only two places it should be 
**                  called from, and they are both inside this file.
**
** journal_mark_dirty -- adds blocks into this transaction.  clears any flags 
**                       that might make them get sent to disk
**                       and then marks them BH_JDirty.  Puts the buffer head 
**                       into the current transaction hash.  
**
** journal_end -- if the current transaction is batchable, it does nothing
**                   otherwise, it could do an async/synchronous commit, or
**                   a full flush of all log and real blocks in the 
**                   transaction.
**
** flush_old_commits -- if the current transaction is too old, it is ended and 
**                      commit blocks are sent to disk.  Forces commit blocks 
**                      to disk for all backgrounded commits that have been 
**                      around too long.
**		     -- Note, if you call this as an immediate flush from 
**		        from within kupdate, it will ignore the immediate flag
**
** The commit thread -- a writer process for async commits.  It allows a 
**                      a process to request a log flush on a task queue.
**                      the commit will happen once the commit thread wakes up.
**                      The benefit here is the writer (with whatever
**                      related locks it has) doesn't have to wait for the
**                      log blocks to hit disk if it doesn't want to.
*/

#ifdef __KERNEL__

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <asm/semaphore.h>

#include <linux/vmalloc.h>
#include <linux/reiserfs_fs.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>

/* the number of mounted filesystems.  This is used to decide when to
** start and kill the commit thread
*/
static int reiserfs_mounted_fs_count = 0 ;

/* wake this up when you add something to the commit thread task queue */
DECLARE_WAIT_QUEUE_HEAD(reiserfs_commit_thread_wait) ;

/* wait on this if you need to be sure you task queue entries have been run */
static DECLARE_WAIT_QUEUE_HEAD(reiserfs_commit_thread_done) ;

/* task queue for async commits, and for the end_io tasks that can't
** be done with interrupts turned off
**
** tasks put on this queue will be run by the commit thread
*/
DECLARE_TASK_QUEUE(reiserfs_commit_thread_tq) ;
DECLARE_TASK_QUEUE(reiserfs_end_io_tq) ;
DECLARE_MUTEX(reiserfs_end_io_sem) ;

#define JOURNAL_TRANS_HALF 1018   /* must be correct to keep the desc and commit
				     structs at 4k */
#define BUFNR 64 /*read ahead */

/* cnode stat bits.  Move these into reiserfs_fs.h */

#define BLOCK_FREED 2		/* this block was freed, and can't be written.  */
#define BLOCK_FREED_HOLDER 3    /* this block was freed during this transaction, and can't be written */

#define BLOCK_NEEDS_FLUSH 4	/* used in flush_journal_list */

/* flags for do_journal_end */
#define FLUSH_ALL   1		/* flush commit and real blocks */
#define COMMIT_NOW  2		/* end and commit this transaction */
#define WAIT        4		/* wait for the log blocks to hit the disk*/

static int do_journal_end(struct reiserfs_transaction_handle *,struct super_block *,unsigned long nblocks,int flags) ;
static void dirty_journal_list(struct super_block *p_s_sb, struct reiserfs_journal_list *jl) ;
static int flush_journal_list(struct super_block *s, struct reiserfs_journal_list *jl, int old_only,int flushall) ;
static int flush_commit_list(struct super_block *s, struct reiserfs_journal_list *jl, int flushall)  ;

static void init_journal_hash(struct super_block *p_s_sb) {
  memset(SB_JOURNAL(p_s_sb)->j_hash_table, 0, JOURNAL_HASH_SIZE * sizeof(struct reiserfs_journal_cnode *)) ;
}

/*
** clears BH_Dirty and sticks the buffer on the clean list.  Called because I can't allow refile_buffer to
** make schedule happen after I've freed a block.  Look at remove_from_transaction and journal_mark_freed for
** more details.
*/
static int reiserfs_clean_and_file_buffer(struct buffer_head *bh) {
  if (bh) {
    clear_bit(BH_Dirty, &bh->b_state) ;
    refile_buffer(bh) ;
  }
  return 0 ;
}

/*
** only call this on FS unmount.
*/
static int free_list_bitmaps(struct super_block *p_s_sb) {
  int i ;
  struct reiserfs_list_bitmap *jb ;
  for (i = 0 ; i < JOURNAL_NUM_BITMAPS ; i++) {
    jb = SB_JOURNAL(p_s_sb)->j_list_bitmap + i ;
    jb->journal_list = NULL ;
    vfree(jb->bitmap) ;
  }
  return 0;
}

/*
** get memory for JOURNAL_NUM_BITMAPS worth of bitmaps.  ick.
*/
static int allocate_list_bitmaps(struct super_block *p_s_sb) {
  int i ;
  int failed = 0 ;
  struct reiserfs_list_bitmap *jb ;
  for (i = 0 ; i < JOURNAL_NUM_BITMAPS ; i++) {
    jb = SB_JOURNAL(p_s_sb)->j_list_bitmap + i ;
    jb->journal_list = NULL ;
    jb->bitmap = vmalloc( ((SB_BLOCK_COUNT(p_s_sb) / 8) + 1) * sizeof(char)) ;
    if (!jb->bitmap) {
      reiserfs_warning("journal-175, unable to allocate bitmaps for journal lists\n") ;
      failed = 1;   
      break ;
    }
    memset(jb->bitmap, 0, ((SB_BLOCK_COUNT(p_s_sb) / 8) + 1) * sizeof(char)) ;
  }
  if (failed) {
    free_list_bitmaps(p_s_sb) ;
    return -1 ;
  }
  return 0 ;
}

/*
** find an available list bitmap.  If you can't find one, flush a commit list and try again
*/
static struct reiserfs_list_bitmap *get_list_bitmap(struct super_block *p_s_sb, struct reiserfs_journal_list *jl) {
  int i,j ; 
  struct reiserfs_list_bitmap *jb = NULL ;
  int windex = push_journal_writer("get_list_bitmap") ;

  for (j = 0 ; j < (JOURNAL_NUM_BITMAPS * 3) ; j++) {
    i = SB_JOURNAL(p_s_sb)->j_list_bitmap_index ;
    SB_JOURNAL(p_s_sb)->j_list_bitmap_index = (i + 1) % JOURNAL_NUM_BITMAPS ;
    jb = SB_JOURNAL(p_s_sb)->j_list_bitmap + i ;
    if (SB_JOURNAL(p_s_sb)->j_list_bitmap[i].journal_list) {
      flush_commit_list(p_s_sb, SB_JOURNAL(p_s_sb)->j_list_bitmap[i].journal_list, 1) ;
      if (!SB_JOURNAL(p_s_sb)->j_list_bitmap[i].journal_list) {
	break ;
      }
    } else {
      break ;
    }
  }
  if (jb->journal_list) { /* double check to make sure if flushed correctly */
    pop_journal_writer(windex) ;
    return NULL ;
  }
  memset(jb->bitmap, 0, ((SB_BLOCK_COUNT(p_s_sb) / 8) + 1) * sizeof(char)) ;
  jb->journal_list = jl ;
  pop_journal_writer(windex) ;
  return jb ;
}

/* 
** allocates a new chunk of X nodes, and links them all together as a list.
** Uses the cnode->next and cnode->prev pointers
** returns NULL on failure
*/
static struct reiserfs_journal_cnode *allocate_cnodes(int num_cnodes) {
  struct reiserfs_journal_cnode *head ;
  int i ;
  if (num_cnodes <= 0) {
    return NULL ;
  }
  head = vmalloc(num_cnodes * sizeof(struct reiserfs_journal_cnode)) ;
  if (!head) {
    return NULL ;
  }
  memset(head, 0, num_cnodes * sizeof(struct reiserfs_journal_cnode)) ;
  head[0].prev = NULL ;
  head[0].next = head + 1 ;
  for (i = 1 ; i < num_cnodes; i++) {
    head[i].prev = head + (i - 1) ;
    head[i].next = head + (i + 1) ; /* if last one, overwrite it after the if */
  }
  head[num_cnodes -1].next = NULL ;
  return head ;
}

/*
** pulls a cnode off the free list, or returns NULL on failure 
*/
static struct reiserfs_journal_cnode *get_cnode(struct super_block *p_s_sb) {
  struct reiserfs_journal_cnode *cn ;
  int windex = push_journal_writer("get_cnode") ;

  reiserfs_check_lock_depth("get_cnode") ;

  if (SB_JOURNAL(p_s_sb)->j_cnode_free <= 0) {
    pop_journal_writer(windex) ;
    return NULL ;
  }
  SB_JOURNAL(p_s_sb)->j_cnode_used++ ;
  SB_JOURNAL(p_s_sb)->j_cnode_free-- ;
  cn = SB_JOURNAL(p_s_sb)->j_cnode_free_list ;
  if (!cn) {
    pop_journal_writer(windex) ;
    return cn ;
  }
  if (cn->next) {
    cn->next->prev = NULL ;
  }
  SB_JOURNAL(p_s_sb)->j_cnode_free_list = cn->next ;
  memset(cn, 0, sizeof(struct reiserfs_journal_cnode)) ;
  pop_journal_writer(windex) ;
  return cn ;
}

/*
** returns a cnode to the free list 
*/
static void free_cnode(struct super_block *p_s_sb, struct reiserfs_journal_cnode *cn) {
  int windex = push_journal_writer("free_cnode") ;

  reiserfs_check_lock_depth("free_cnode") ;

  SB_JOURNAL(p_s_sb)->j_cnode_used-- ;
  SB_JOURNAL(p_s_sb)->j_cnode_free++ ;
  /* memset(cn, 0, sizeof(struct reiserfs_journal_cnode)) ; */
  cn->next = SB_JOURNAL(p_s_sb)->j_cnode_free_list ;
  if (SB_JOURNAL(p_s_sb)->j_cnode_free_list) {
    SB_JOURNAL(p_s_sb)->j_cnode_free_list->prev = cn ;
  }
  cn->prev = NULL ; /* not needed with the memset, but I might kill the memset, and forget to do this */
  SB_JOURNAL(p_s_sb)->j_cnode_free_list = cn ;
  pop_journal_writer(windex) ;
}

static int clear_prepared_bits(struct buffer_head *bh) {
  clear_bit(BH_JPrepared, &bh->b_state) ;
  clear_bit(BH_JRestore_dirty, &bh->b_state) ;
  return 0 ;
}

/* buffer is in current transaction */
inline int buffer_journaled(const struct buffer_head *bh) {
  if (bh)
    return test_bit(BH_JDirty, ( struct buffer_head * ) &bh->b_state) ;
  else
    return 0 ;
}

/* disk block was taken off free list before being in a finished transation, or written to disk
** journal_new blocks can be reused immediately, for any purpose
*/ 
inline int buffer_journal_new(const struct buffer_head *bh) {
  if (bh) 
    return test_bit(BH_JNew, ( struct buffer_head * )&bh->b_state) ;
  else
    return 0 ;
}

inline int mark_buffer_journal_new(struct buffer_head *bh) {
  if (bh) {
    set_bit(BH_JNew, &bh->b_state) ;
  }
  return 0 ;
}

inline int mark_buffer_not_journaled(struct buffer_head *bh) {
  if (bh) 
    clear_bit(BH_JDirty, &bh->b_state) ;
  return 0 ;
}

void reiserfs_check_lock_depth(char *caller) {
#ifdef __SMP__
  if (current->lock_depth < 0) {
    char *crashit = NULL ;
    printk("%s called without kernel lock held\n", caller) ;
    show_reiserfs_locks() ;
    *crashit = 1 ;
  }
#else
  ;
#endif
}

/* return a cnode with same dev, block number and size in table, or null if not found */
static inline struct reiserfs_journal_cnode *get_journal_hash_dev(struct reiserfs_journal_cnode **table,
                                   				  kdev_t dev,long bl,int size) {
  struct reiserfs_journal_cnode *cn ;
  cn = journal_hash(table, dev, bl) ;
  while(cn) {
    if ((cn->blocknr == bl) && (cn->dev == dev))
      return cn ;
    cn = cn->hnext ;
  }
  return (struct reiserfs_journal_cnode *)0 ;
}

/* returns a cnode with same size, block number and dev as bh in the current transaction hash.  NULL if not found */
static inline struct reiserfs_journal_cnode *get_journal_hash(struct super_block *p_s_sb, struct buffer_head *bh) {
  struct reiserfs_journal_cnode *cn ;
  if (bh) {
    cn =  get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_hash_table, bh->b_dev, bh->b_blocknr, bh->b_size) ;
  }
  else {
    return (struct reiserfs_journal_cnode *)0 ;
  }
  return cn ;
}

/*
** once upon a time, the journal would deadlock.  a lot.  Now, which CONFIG_REISERFS_CHECK, anytime someone enters a
** transaction, it pushes itself into this ugly static list, and pops itself off before calling journal_end.
** I made a SysRq key to dump the list, and tell me what the writers are when I'm deadlocked.
*/
static char *journal_writers[512] ;
int push_journal_writer(char *s) {
#ifdef CONFIG_REISERFS_CHECK
  int i ;
  for (i = 0 ; i < 512 ; i++) {
    if (!journal_writers[i]) {
      journal_writers[i] = s ;
      return i ;
    }
  }
  return -1 ;
#else
  return 0 ;
#endif
}
int pop_journal_writer(int index) {
#ifdef CONFIG_REISERFS_CHECK
  if (index >= 0) {
    journal_writers[index] = NULL ;
  }
#endif
  return 0 ;
}
int dump_journal_writers(void) {
  int i ;
  for (i = 0 ; i < 512 ; i++) {
    if (journal_writers[i]) {
      printk("%d: %s\n", i, journal_writers[i]) ;
    }
  }
  return 0 ;
}

/*
** this actually means 'can this block be reallocated yet?'.  If you set search_all, a block can only be allocated
** if it is not in the current transaction, was not freed by the current transaction, and has no chance of ever
** being overwritten by a replay after crashing.
**
** If you don't set search_all, a block can only be allocated if it is not in the current transaction.  Since deleting
** a block removes it from the current transaction, this case should never happen.  If you don't set search_all, make
** sure you never write the block without logging it.
**
** next_zero_bit is a suggestion about the next block to try for find_forward.
** when bl is rejected because it is set in a journal list bitmap, we search
** for the next zero bit in the bitmap that rejected bl.  Then, we return that
** through next_zero_bit for find_forward to try.
**
** Just because we return something in next_zero_bit does not mean we won't
** reject it on the next call to reiserfs_in_journal
**
*/
int reiserfs_in_journal(struct super_block *p_s_sb, kdev_t dev, unsigned long bl, int size, int search_all, unsigned long *next_zero_bit) {
  struct reiserfs_journal_cnode *cn ;
  struct reiserfs_list_bitmap *jb ;
  int i ;
  int windex; 
  
  if (reiserfs_dont_log(p_s_sb)) {
    return 0 ;
  }

  windex = push_journal_writer("reiserfs_in_journal") ;

  *next_zero_bit = 0 ; /* always start this at zero. */

  PROC_INFO_INC( p_s_sb, journal.in_journal );
  /* If we aren't doing a search_all, this is a metablock, and it will be logged before use.
  ** if we crash before the transaction that freed it commits,  this transaction won't
  ** have committed either, and the block will never be written
  */
  if (search_all) {
    for (i = 0 ; i < JOURNAL_NUM_BITMAPS ; i++) {
      PROC_INFO_INC( p_s_sb, journal.in_journal_bitmap );
      jb = SB_JOURNAL(p_s_sb)->j_list_bitmap + i ;
      if (jb->journal_list && test_bit(bl, jb->bitmap)) {
	*next_zero_bit = find_next_zero_bit((unsigned long *)(jb->bitmap),
	                                   SB_BLOCK_COUNT(p_s_sb), bl+1) ; 
	pop_journal_writer(windex) ;
	return 1 ;
      }
    }
  }

  /* is it in any old transactions? */
  if (search_all && (cn = get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_list_hash_table, dev,bl,size))) {
    pop_journal_writer(windex) ;
    return 1; 
  }

  /* is it in the current transaction.  This should never happen */
  if ((cn = get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_hash_table, dev,bl,size))) {
    pop_journal_writer(windex) ;
    return 1; 
  }

  PROC_INFO_INC( p_s_sb, journal.in_journal_reusable );
  /* safe for reuse */
  pop_journal_writer(windex) ;
  return 0 ;
}

/* insert cn into table
*/
inline void insert_journal_hash(struct reiserfs_journal_cnode **table, struct reiserfs_journal_cnode *cn) {
  struct reiserfs_journal_cnode *cn_orig ;

  cn_orig = journal_hash(table, cn->dev, cn->blocknr) ;
  cn->hnext = cn_orig ;
  cn->hprev = NULL ;
  if (cn_orig) {
    cn_orig->hprev = cn ;
  }
  journal_hash(table, cn->dev, cn->blocknr) =  cn ;
}

/* lock the current transaction */
inline static void lock_journal(struct super_block *p_s_sb) {
  int windex = push_journal_writer("lock_journal") ;
  while(atomic_read(&(SB_JOURNAL(p_s_sb)->j_wlock)) > 0) {
    sleep_on(&(SB_JOURNAL(p_s_sb)->j_wait)) ;
  }
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_wlock), 1) ;
  pop_journal_writer(windex) ;
}

/* unlock the current transaction */
inline static void unlock_journal(struct super_block *p_s_sb) {
  atomic_dec(&(SB_JOURNAL(p_s_sb)->j_wlock)) ;
  wake_up(&(SB_JOURNAL(p_s_sb)->j_wait)) ;
}

/*
** this used to be much more involved, and I'm keeping it just in case things get ugly again.
** it gets called by flush_commit_list, and cleans up any data stored about blocks freed during a
** transaction.
*/
static void cleanup_freed_for_journal_list(struct super_block *p_s_sb, struct reiserfs_journal_list *jl) {

  jl->j_list_bitmap->journal_list = NULL ;
  jl->j_list_bitmap = NULL ;
}

/*
** if this journal list still has commit blocks unflushed, send them to disk.
**
** log areas must be flushed in order (transaction 2 can't commit before transaction 1)
** Before the commit block can by written, every other log block must be safely on disk
**
*/
static int flush_commit_list(struct super_block *s, struct reiserfs_journal_list *jl, int flushall) {
  int i, count ;
  int index = 0 ;
  int bn ;
  int retry_count = 0 ;
  int orig_commit_left = 0 ;
  struct buffer_head *tbh = NULL ;
  struct reiserfs_journal_list *other_jl ;
  int windex = push_journal_writer("flush_commit_list") ;

  reiserfs_check_lock_depth("flush_commit_list") ;

  if (atomic_read(&jl->j_older_commits_done)) {
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* before we can put our commit blocks on disk, we have to make sure everyone older than
  ** us is on disk too
  */
  if (jl->j_len <= 0) {
    pop_journal_writer(windex) ;
    return 0 ;
  }
  if (flushall) {
    /* we _must_ make sure the transactions are committed in order.  Start with the
    ** index after this one, wrap all the way around 
    */
    index = (jl - SB_JOURNAL_LIST(s)) + 1 ;
    for (i = 0 ; i < JOURNAL_LIST_COUNT ; i++) {
      other_jl = SB_JOURNAL_LIST(s) + ( (index + i) % JOURNAL_LIST_COUNT) ;
      if (other_jl && other_jl != jl && other_jl->j_len > 0 && other_jl->j_trans_id > 0 && 
          other_jl->j_trans_id <= jl->j_trans_id && (atomic_read(&(jl->j_older_commits_done)) == 0)) {
        flush_commit_list(s, other_jl, 0) ;
      }
    }
  }

  count = 0 ;
  /* don't flush the commit list for the current transactoin */
  if (jl == ((SB_JOURNAL_LIST(s) + SB_JOURNAL_LIST_INDEX(s)))) {
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* make sure nobody is trying to flush this one at the same time */
  if (atomic_read(&(jl->j_commit_flushing))) {
    sleep_on(&(jl->j_commit_wait)) ;
    if (flushall) {
      atomic_set(&(jl->j_older_commits_done), 1) ;
    }
    pop_journal_writer(windex) ;
    return 0 ;
  }
  
  /* this commit is done, exit */
  if (atomic_read(&(jl->j_commit_left)) <= 0) {
    if (flushall) {
      atomic_set(&(jl->j_older_commits_done), 1) ;
    }
    pop_journal_writer(windex) ;
    return 0 ;
  }
  atomic_set(&(jl->j_commit_flushing), 1) ; /* keeps end_io from doing anything with commit_bh, and 
                                            others from flushing while we are flushing */


  if (jl->j_len > JOURNAL_TRANS_MAX) {
    reiserfs_panic(s, "journal-512: flush_commit_list: length is %lu, list number %d\n", jl->j_len, jl - SB_JOURNAL_LIST(s)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  }

  orig_commit_left = atomic_read(&(jl->j_commit_left)) ; /* why? as log blocks get written j_commit_left goes 
                                                            down without us knowing */

  /* start by checking all the commit blocks in this transaction.  
  ** Add anyone not on disk into tbh.  Stop checking once commit_left <= 1, because that means we
  ** only have the commit block left 
  */
retry:
  count = 0 ;
  for (i = 0 ; atomic_read(&(jl->j_commit_left)) > 1 && i < (jl->j_len + 1) ; i++) {  /* everything but commit_bh */
    bn = SB_JOURNAL_BLOCK(s) + (jl->j_start + i) % JOURNAL_BLOCK_COUNT  ;
    tbh = get_hash_table(s->s_dev, bn, s->s_blocksize) ;

/* kill this sanity check */
if (count > (orig_commit_left + 2)) {
reiserfs_panic(s, "journal-539: flush_commit_list: BAD count(%d) > orig_commit_left(%d)!\n", count, orig_commit_left) ;
}
    if (tbh) {
      if (buffer_locked(tbh)) { /* wait on it, redo it just to make sure */
	wait_on_buffer(tbh) ;
	if (!buffer_uptodate(tbh)) {
	  reiserfs_panic(s, "journal-584, buffer write failed\n") ;
	}
      } 
      if (buffer_dirty(tbh)) {
	printk("journal-569: flush_commit_list, block already dirty!\n") ;
      } else {				
	mark_buffer_dirty(tbh, 0) ; 
      }
      tbh->b_end_io = reiserfs_journal_end_io ; /* not needed */
      ll_rw_block(WRITE, 1, &tbh) ;
      count++ ;
      put_bh(tbh) ; /* once for our get_hash */
    } 
  }

  /* wait on everyone in tbh before writing commit block*/
  if (count > 0) {
    for (i = 0 ; atomic_read(&(jl->j_commit_left)) > 1 && 
                 i < (jl->j_len + 1) ; i++) {  /* everything but commit_bh */
      bn = SB_JOURNAL_BLOCK(s) + (jl->j_start + i) % JOURNAL_BLOCK_COUNT  ;
      tbh = get_hash_table(s->s_dev, bn, s->s_blocksize) ;

      wait_on_buffer(tbh) ;
      if (!buffer_uptodate(tbh)) {
	reiserfs_panic(s, "journal-601, buffer write failed\n") ;
      }
      atomic_dec(&(tbh->b_count)) ; /* once for our get_hash */
      brelse(tbh) ;    /* once due to original getblk in do_journal_end */
      atomic_dec(&(jl->j_commit_left)) ;
    }
  }

  if (atomic_read(&(jl->j_commit_left)) != 1) { /* just the commit_bh left, flush it without calling getblk for everyone */
    if (retry_count < 2) {
      printk("journal-582: flush_commit_list, not all log blocks on disk yet, trying again\n") ;
      retry_count++ ;
      goto retry;
    }
    reiserfs_panic(s, "journal-563: flush_commit_list: BAD, j_commit_left is %u, should be 1\n", 
		   atomic_read(&(jl->j_commit_left)));
  }

  mark_buffer_dirty(jl->j_commit_bh,0) ; 
  ll_rw_block(WRITE, 1, &(jl->j_commit_bh)) ;
  wait_on_buffer(jl->j_commit_bh) ;
  if (!buffer_uptodate(jl->j_commit_bh)) {
    reiserfs_panic(s, "journal-615: buffer write failed\n") ;
  }
  atomic_dec(&(jl->j_commit_left)) ;
  brelse(jl->j_commit_bh) ;

  /* now, every commit block is on the disk.  It is safe to allow blocks freed during this transaction to be reallocated */
  cleanup_freed_for_journal_list(s, jl) ;

  /* and it is safe to dirty/release all the real buffer heads */
  dirty_journal_list(s, jl) ;

  if (flushall) {
    atomic_set(&(jl->j_older_commits_done), 1) ;
  }
  atomic_set(&(jl->j_commit_flushing), 0) ;
  wake_up(&(jl->j_commit_wait)) ;
  pop_journal_writer(windex) ;
  return 0 ;
}

/*
** flush_journal_list frequently needs to find a newer transaction for a given block.  This does that, or 
** returns NULL if it can't find anything 
*/
static struct reiserfs_journal_list *find_newer_jl_for_cn(struct reiserfs_journal_cnode *cn) {
  kdev_t dev = cn->dev;
  unsigned long blocknr = cn->blocknr ;

  cn = cn->hprev ;
  while(cn) {
    if (cn->dev == dev && cn->blocknr == blocknr && cn->jlist) {
      return cn->jlist ;
    }
    cn = cn->hprev ;
  }
  return NULL ;
}


/*
** once all the real blocks have been flushed, it is safe to remove them from the
** journal list for this transaction.  Aside from freeing the cnode, this also allows the
** block to be reallocated for data blocks if it had been deleted.
*/
static void remove_all_from_journal_list(struct super_block *p_s_sb, struct reiserfs_journal_list *jl, int debug) {
  struct buffer_head fake_bh ;
  struct reiserfs_journal_cnode *cn, *last ;
  int windex = push_journal_writer("remove_all_from_journal_list") ;
  cn = jl->j_realblock ;

  /* which is better, to lock once around the whole loop, or
  ** to lock for each call to remove_from_journal_list?
  */
  while(cn) {
    if (cn->blocknr != 0) {
      if (debug) {
        printk("block %lu, bh is %d, state %ld\n", cn->blocknr, cn->bh ? 1: 0, 
	        cn->state) ;
      }
      fake_bh.b_blocknr = cn->blocknr ;
      fake_bh.b_dev = cn->dev ;
      cn->state = 0 ;
      remove_from_journal_list(p_s_sb, jl, &fake_bh, 1) ;
    }
    last = cn ;
    cn = cn->next ;
    free_cnode(p_s_sb, last) ;
  }
  jl->j_realblock = NULL ;
  pop_journal_writer(windex) ;
}

/*
** if this timestamp is greater than the timestamp we wrote last to the header block, write it to the header block.
** once this is done, I can safely say the log area for this transaction won't ever be replayed, and I can start
** releasing blocks in this transaction for reuse as data blocks.
** called by flush_journal_list, before it calls remove_all_from_journal_list
**
*/
static int _update_journal_header_block(struct super_block *p_s_sb, unsigned long offset, unsigned long trans_id) {
  struct reiserfs_journal_header *jh ;
  if (trans_id >= SB_JOURNAL(p_s_sb)->j_last_flush_trans_id) {
    if (buffer_locked((SB_JOURNAL(p_s_sb)->j_header_bh)))  {
      wait_on_buffer((SB_JOURNAL(p_s_sb)->j_header_bh)) ;
      if (!buffer_uptodate(SB_JOURNAL(p_s_sb)->j_header_bh)) {
        reiserfs_panic(p_s_sb, "journal-699: buffer write failed\n") ;
      }
    }
    SB_JOURNAL(p_s_sb)->j_last_flush_trans_id = trans_id ;
    SB_JOURNAL(p_s_sb)->j_first_unflushed_offset = offset ;
    jh = (struct reiserfs_journal_header *)(SB_JOURNAL(p_s_sb)->j_header_bh->b_data) ;
    jh->j_last_flush_trans_id = trans_id ;
    jh->j_first_unflushed_offset = offset ;
    jh->j_mount_id = SB_JOURNAL(p_s_sb)->j_mount_id ;
    set_bit(BH_Dirty, &(SB_JOURNAL(p_s_sb)->j_header_bh->b_state)) ;
    ll_rw_block(WRITE, 1, &(SB_JOURNAL(p_s_sb)->j_header_bh)) ;
    wait_on_buffer((SB_JOURNAL(p_s_sb)->j_header_bh)) ; 
    if (!buffer_uptodate(SB_JOURNAL(p_s_sb)->j_header_bh)) {
      reiserfs_panic(p_s_sb, "journal-712: buffer write failed\n") ;
    }
  }
  return 0 ;
}

/*
** returns 1 if all older journal lists have been flushed
*/
static int older_journal_lists_are_flushed(struct super_block *p_s_sb, unsigned long trans_id) {
  int i ;
  struct reiserfs_journal_list *jl ;
  for (i = 0 ; i < JOURNAL_LIST_COUNT ; i++) {
    jl = SB_JOURNAL_LIST(p_s_sb) + i ;
    if (jl && jl->j_len > 0 && jl->j_trans_id < trans_id && atomic_read(&(jl->j_nonzerolen)) > 0) {
      return 0 ;
    }
  }
  return 1 ;
}

/* 
** flush any and all journal lists older than you are 
** can only be called from flush_journal_list
*/
static int flush_older_journal_lists(struct super_block *p_s_sb, struct reiserfs_journal_list *jl, unsigned long trans_id) {
  int i, index ;
  struct reiserfs_journal_list *other_jl ;

  index = jl - SB_JOURNAL_LIST(p_s_sb) ;
  for (i = 0 ; i < JOURNAL_LIST_COUNT ; i++) {
    other_jl = SB_JOURNAL_LIST(p_s_sb) + ((index + i) % JOURNAL_LIST_COUNT) ;
    if (other_jl && other_jl->j_len > 0 && 
        other_jl->j_trans_id > 0 && 
	other_jl->j_trans_id < trans_id && 
        other_jl != jl) {
      /* not old only, not flush all */
      flush_journal_list(p_s_sb, other_jl, 0, 0) ; 
    }
  }
  return 0 ;
}

/* flush a journal list, both commit and real blocks
** set old_only to one if you only want to touch journal_lists that are fully flushed and done with.  This allows
** you to free the memory they are using
**
** always set flushall to 1, unless you are flushing all of them, or you are calling from inside
** flush_journal_list
**
** IMPORTANT.  This can only be called while there are no journal writers, and the journal is locked.  That means
** it can only be called from do_journal_end.  If you set old_only, you can call from other places.  journal_release
** can call this because there aren't any writers then.
*/
static int flush_journal_list(struct super_block *s, struct reiserfs_journal_list *jl, int old_only, int flushall) {
  struct reiserfs_journal_list *pjl ;
  struct reiserfs_journal_cnode *cn, *last ;
  int count ;
  int was_jwait = 0 ;
  int was_dirty = 0 ;
  struct buffer_head *saved_bh ; 
  unsigned long j_len_saved = jl->j_len ;
  int windex = push_journal_writer("flush_journal_list") ;

  if (j_len_saved <= 0) {
    pop_journal_writer(windex) ;
    return 0 ;
  }
  /* pretest to avoid the locking */
  if (old_only && (atomic_read(&(jl->j_nonzerolen)) > 0 || 
                   atomic_read(&(jl->j_flushing)))) {
    pop_journal_writer(windex) ;
    return 0 ;
  }
  while (atomic_read(&(jl->j_commit_flushing)) && !old_only) { /* if someone is getting the commit list, we must wait for them */
    sleep_on(&(jl->j_commit_wait)) ;
  }
  /* if someone is flushing this list, we must wait for them */
  while (atomic_read(&(jl->j_flushing))) {
    sleep_on(&(jl->j_flush_wait)) ;
  }

  /* this list is now ours, we can change anything we want */
  atomic_set(&(jl->j_flushing), 1) ;

  count = 0 ;
  if (j_len_saved > JOURNAL_TRANS_MAX) {
    reiserfs_panic(s, "journal-715: flush_journal_list, length is %lu, list number %d\n", j_len_saved, jl - SB_JOURNAL_LIST(s)) ;
    atomic_dec(&(jl->j_flushing)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* if all the work is already done, get out of here */
  if (atomic_read(&(jl->j_nonzerolen)) <= 0 && atomic_read(&(jl->j_commit_left)) <= 0) {
    if (flushall) {
      flush_older_journal_lists(s, jl, jl->j_trans_id) ;
    } else if (old_only && !older_journal_lists_are_flushed(s, jl->j_trans_id)) { /* only flush if we were called old_only */
      atomic_dec(&(jl->j_flushing)) ;
      wake_up(&(jl->j_flush_wait)) ;
      pop_journal_writer(windex) ;
      return 0 ;
    } 
    update_journal_header_block(s, (jl->j_start + jl->j_len + 2) % JOURNAL_BLOCK_COUNT, jl->j_trans_id) ;
    remove_all_from_journal_list(s, jl, 0) ;
    jl->j_len = 0 ;
    jl->j_start = 0 ;
    jl->j_commit_bh = NULL ;
    jl->j_trans_id = 0 ;
    atomic_dec(&(jl->j_flushing)) ;
    wake_up(&(jl->j_flush_wait)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  } 

  /* if we were called old_only, we're done. */
  if (old_only) { 
    atomic_dec(&(jl->j_flushing)) ;
    wake_up(&(jl->j_flush_wait)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* not old only, start by putting the commit list on disk.  This will also flush the commit lists of any
  ** olders transactions, which is important
  */
  flush_commit_list(s, jl, 1) ;

  /* are we done now? */
  if (atomic_read(&(jl->j_nonzerolen)) <= 0 && atomic_read(&(jl->j_commit_left)) <= 0) {
    if (flushall) {
      flush_older_journal_lists(s, jl, jl->j_trans_id) ;
    } 
    update_journal_header_block(s, (jl->j_start + jl->j_len + 2) % JOURNAL_BLOCK_COUNT, jl->j_trans_id) ;
    remove_all_from_journal_list(s, jl, 0) ;
    jl->j_len = 0 ;
    jl->j_start = 0 ;
    jl->j_commit_bh = NULL ;
    jl->j_trans_id = 0 ;
    atomic_dec(&(jl->j_flushing)) ;
    wake_up(&(jl->j_flush_wait)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* loop through each cnode, see if we need to write it, or wait on a more recent transaction, or just ignore it */
  if (atomic_read(&(SB_JOURNAL(s)->j_wcount)) != 0) {
    reiserfs_panic(s, "journal-844: panic journal list is flushing, wcount is not 0\n") ;
  }
  cn = jl->j_realblock ;
  while(cn) {
    was_jwait = 0 ;
    was_dirty = 0 ;
    saved_bh = NULL ;
    /* blocknr of 0 is no longer in the hash, ignore it */
    if (cn->blocknr == 0) {
      goto free_cnode ;
    }
    pjl = find_newer_jl_for_cn(cn) ;
    /* the order is important here.  We check pjl to make sure we
    ** don't clear BH_JDirty_wait if we aren't the one writing this
    ** block to disk
    */
    if (!pjl && cn->bh) {
      saved_bh = cn->bh ;

      /* we do this to make sure nobody releases the buffer while we are working with it */
      atomic_inc(&(saved_bh->b_count)) ;  

      if (buffer_journal_dirty(saved_bh)) {
        was_jwait = 1 ;
	mark_buffer_notjournal_dirty(saved_bh) ;
	atomic_dec(&(saved_bh->b_count)) ; /* brelse the inc from journal_mark_dirty */
      }
      if (buffer_dirty(saved_bh)) {
        was_dirty = 1 ;
      }
    }

    /* if someone has this block in a newer transaction, just make
    ** sure they are commited, and don't try writing it to disk
    */
    if (pjl) {
      flush_commit_list(s, pjl, 1) ;
      goto free_cnode ;
    }

    /* bh == NULL when the block got to disk on its own, OR, the block got freed in a future transaction */
    if (saved_bh == NULL) {
      goto free_cnode ;
    }

    /* the end_io task might not have run the buffer yet, so it is possible 
    ** to have jwait buffer that isn't dirty.  It is not possible to have 
    ** a buffer here that isn't mark BH_JDirty_wait
    */

    if ((!was_jwait) && !buffer_locked(saved_bh)) {
printk("journal-813: BAD! buffer %lu %cdirty %cjwait, not in a newer tranasction\n", saved_bh->b_blocknr,
        was_dirty ? ' ' : '!', was_jwait ? ' ' : '!') ;
    }
    /* if it is locked, we wait on it so the end_io handler does not clobber something we are doing
    ** we'll clear it out of the hash at the end with remove_all
    */
    if (buffer_locked(saved_bh)) {
      wait_on_buffer(saved_bh) ;
      if (!buffer_uptodate(saved_bh)) {
        reiserfs_panic(s, "journal-923: buffer write failed\n") ;
      }
      goto free_cnode ;
    } else if (buffer_dirty(saved_bh)) { /* it is still dirty, send to disk */
      /* we inc again because saved_bh gets decremented at free_cnode */
      get_bh(saved_bh) ;
      set_bit(BLOCK_NEEDS_FLUSH, &cn->state) ;
      ll_rw_block(WRITE, 1, &saved_bh) ;
      count++ ;
    } 
free_cnode:
    last = cn ;
    cn = cn->next ;
    if (saved_bh) {
      atomic_dec(&(saved_bh->b_count)); /* we incremented this to keep others from taking the buffer head away */
      if (atomic_read(&(saved_bh->b_count)) < 0) {
        printk("journal-945: saved_bh->b_count < 0") ;
      }
    }
  }
  if (count > 0) {
    cn = jl->j_realblock ;
    while(cn) {
      if (test_bit(BLOCK_NEEDS_FLUSH, &cn->state)) {
	if (!cn->bh) {
	  reiserfs_panic(s, "journal-1011: cn->bh is NULL\n") ;
	}
	wait_on_buffer(cn->bh) ;
	if (!cn->bh) {
	  reiserfs_panic(s, "journal-1012: cn->bh is NULL\n") ;
	}
	if (!buffer_uptodate(cn->bh)) {
	  reiserfs_panic(s, "journal-949: buffer write failed\n") ;
	}
        brelse(cn->bh) ;
      }
      cn = cn->next ;
    }
  }

  /* before we can update the journal header block, we _must_ flush all real blocks from all older transactions to disk */
  if (flushall) {
    flush_older_journal_lists(s, jl, jl->j_trans_id) ;
  } 
  
  /* before we can remove everything from the hash tables for this transaction, we must make sure it can
  ** never be replayed
  */
  update_journal_header_block(s, (jl->j_start + jl->j_len + 2) % JOURNAL_BLOCK_COUNT, jl->j_trans_id) ;
  remove_all_from_journal_list(s, jl, 0) ;
  jl->j_len = 0 ;
  atomic_set(&(jl->j_nonzerolen), 0) ;
  jl->j_start = 0 ;
  jl->j_realblock = NULL ;
  jl->j_commit_bh = NULL ;
  jl->j_trans_id = 0 ;
  atomic_dec(&(jl->j_flushing)) ;
  wake_up(&(jl->j_flush_wait)) ;
  pop_journal_writer(windex) ;
  return 0 ;
} 


/*
** removes any nodes in table with name block and dev as bh.
** only touchs the hnext and hprev pointers.
*/
void remove_journal_hash(struct reiserfs_journal_cnode **table, struct reiserfs_journal_list *jl,struct buffer_head *bh,
                         int remove_freed){
  struct reiserfs_journal_cnode *cur ;
  struct reiserfs_journal_cnode **head ;

  if (!bh)
    return ;

  head= &(journal_hash(table, bh->b_dev, bh->b_blocknr)) ;
  if (!head) {
    return ;
  }
  cur = *head ;
  while(cur) {
    if (cur->blocknr == bh->b_blocknr && cur->dev == bh->b_dev && (jl == NULL || jl == cur->jlist) && 
        (!test_bit(BLOCK_FREED, &cur->state) || remove_freed)) {
      if (cur->hnext) {
        cur->hnext->hprev = cur->hprev ;
      }
      if (cur->hprev) {
	cur->hprev->hnext = cur->hnext ;
      } else {
	*head = cur->hnext ;
      }
      cur->blocknr = 0 ;
      cur->dev = 0 ;
      cur->state = 0 ;
      if (cur->bh && cur->jlist) /* anybody who clears the cur->bh will also dec the nonzerolen */
	atomic_dec(&(cur->jlist->j_nonzerolen)) ;
      cur->bh = NULL ;
      cur->jlist = NULL ;
    } 
    cur = cur->hnext ;
  }
}
/*
** call on unmount.  Only set error to 1 if you haven't made your way out
** of read_super() yet.  Any other caller must keep error at 0.
*/
static int do_journal_release(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, int error) {
  struct reiserfs_transaction_handle myth ;

  /* we only want to flush out transactions if we were called with error == 0
  */
  if (!error) {
    /* end the current trans */
    do_journal_end(th, p_s_sb,10, FLUSH_ALL) ;

    /* make sure something gets logged to force our way into the flush code */
    journal_join(&myth, p_s_sb, 1) ;
    reiserfs_prepare_for_journal(p_s_sb, SB_BUFFER_WITH_SB(p_s_sb), 1) ;
    journal_mark_dirty(&myth, p_s_sb, SB_BUFFER_WITH_SB(p_s_sb)) ;
    do_journal_end(&myth, p_s_sb,1, FLUSH_ALL) ;

    /* get any stragglers from the task queue */
    down(&reiserfs_end_io_sem) ;
    run_task_queue(&reiserfs_end_io_tq) ;
    up(&reiserfs_end_io_sem) ;
  }

  /* we decrement before we wake up, because the commit thread dies off
  ** when it has been woken up and the count is <= 0
  */
  reiserfs_mounted_fs_count-- ;
  wake_up(&reiserfs_commit_thread_wait) ;
  sleep_on(&reiserfs_commit_thread_done) ;

  vfree(SB_JOURNAL(p_s_sb)->j_cnode_free_orig) ;
  free_list_bitmaps(p_s_sb) ;
  brelse(SB_JOURNAL(p_s_sb)->j_header_bh) ;
  vfree(SB_JOURNAL(p_s_sb)) ;

  return 0 ;
}

/*
** call on unmount.  flush all journal trans, release all alloc'd ram
*/
int journal_release(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb) {
  return do_journal_release(th, p_s_sb, 0) ;
}
/*
** only call from an error condition inside reiserfs_read_super!
*/
int journal_release_error(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb) {
  return do_journal_release(th, p_s_sb, 1) ;
}

/* compares description block with commit block.  returns 1 if they differ, 0 if they are the same */
static int journal_compare_desc_commit(struct super_block *p_s_sb, struct reiserfs_journal_desc *desc, 
			               struct reiserfs_journal_commit *commit) {
  if (commit->j_trans_id != desc->j_trans_id || commit->j_len != desc->j_len || commit->j_len > JOURNAL_TRANS_MAX || 
      commit->j_len <= 0 
  ) {
    return 1 ;
  }
  return 0 ;
}
/* returns 0 if it did not find a description block  
** returns -1 if it found a corrupt commit block
** returns 1 if both desc and commit were valid 
*/
static int journal_transaction_is_valid(struct super_block *p_s_sb, struct buffer_head *d_bh, unsigned long *oldest_invalid_trans_id, unsigned long *newest_mount_id) {
  struct reiserfs_journal_desc *desc ;
  struct reiserfs_journal_commit *commit ;
  struct reiserfs_super_block *rs = SB_DISK_SUPER_BLOCK(p_s_sb) ;
  struct buffer_head *c_bh ;
  unsigned long offset ;

  if (!d_bh)
      return 0 ;

  desc = (struct reiserfs_journal_desc *)d_bh->b_data ;
  if (desc->j_len > 0 && !memcmp(desc->j_magic, JOURNAL_DESC_MAGIC, 8)) {
    if (oldest_invalid_trans_id && *oldest_invalid_trans_id && desc->j_trans_id > *oldest_invalid_trans_id) {
printk("journal-986: transaction is valid returning because trans_id %d is greater than oldest_invalid %lu\n", 
        desc->j_trans_id, *oldest_invalid_trans_id) ;
      return 0 ;
    }
    if (newest_mount_id && *newest_mount_id > desc->j_mount_id) {
      printk("journal-1087: transaction is valid returning because mount_id %d is less than newest_mount_id %lu\n", desc->j_mount_id, *newest_mount_id) ;
      return -1 ;
    }
    offset = d_bh->b_blocknr - rs->s_journal_block ;

    /* ok, we have a journal description block, lets see if the transaction was valid */
    c_bh = bread(p_s_sb->s_dev, rs->s_journal_block + ((offset + desc->j_len + 1) % JOURNAL_BLOCK_COUNT), 
    		p_s_sb->s_blocksize) ;
    if (!c_bh)
      return 0 ;
    commit = (struct reiserfs_journal_commit *)c_bh->b_data ;
    if (journal_compare_desc_commit(p_s_sb, desc, commit)) {
      printk("journal_transaction_is_valid, commit offset %ld had bad time %d or length %d\n", 
	      c_bh->b_blocknr - rs->s_journal_block,commit->j_trans_id, commit->j_len);
      brelse(c_bh) ;
      if (oldest_invalid_trans_id)
        *oldest_invalid_trans_id = desc->j_trans_id ;
printk("journal-1004: transaction_is_valid setting oldest invalid trans_id to %d\n", desc->j_trans_id) ;
      return -1; 
    }
    brelse(c_bh) ;
printk("journal-1006: found valid transaction start offset %lu, len %d id %d\n", d_bh->b_blocknr - rs->s_journal_block , desc->j_len, desc->j_trans_id) ;
    return 1 ;
  } else {
    return 0 ;
  }
}

static void brelse_array(struct buffer_head **heads, int num) {
  int i ;
  for (i = 0 ; i < num ; i++) {
    brelse(heads[i]) ;
  }
}

/*
** given the start, and values for the oldest acceptable transactions,
** this either reads in a replays a transaction, or returns because the transaction
** is invalid, or too old.
*/
static int journal_read_transaction(struct super_block *p_s_sb, unsigned long cur_dblock, unsigned long oldest_start, 
				    unsigned long oldest_trans_id, unsigned long newest_mount_id) {
  struct reiserfs_journal_desc *desc ;
  struct reiserfs_journal_commit *commit ;
  struct reiserfs_super_block *rs = SB_DISK_SUPER_BLOCK(p_s_sb) ;
  unsigned long trans_id = 0 ;
  struct buffer_head *c_bh ;
  struct buffer_head *d_bh ;
  struct buffer_head **log_blocks = NULL ;
  struct buffer_head **real_blocks = NULL ;
  unsigned long trans_offset ;
  int i;

  d_bh = sb_bread(p_s_sb, cur_dblock) ;
  if (!d_bh)
    return 1 ;
  desc = (struct reiserfs_journal_desc *)d_bh->b_data ;
  trans_offset = d_bh->b_blocknr - rs->s_journal_block ;
printk("journal-1037: journal_read_transaction, offset %lu, len %d mount_id %d\n", d_bh->b_blocknr - rs->s_journal_block, desc->j_len, desc->j_mount_id) ;
  if (desc->j_trans_id < oldest_trans_id) {
printk("journal-1039: journal_read_trans skipping because %lu is too old\n", cur_dblock - rs->s_journal_block) ;
    brelse(d_bh) ;
    return 1 ;
  }
  if (desc->j_mount_id != newest_mount_id) {
printk("journal-1146: journal_read_trans skipping because %d is != newest_mount_id %lu\n", desc->j_mount_id, newest_mount_id) ;
    brelse(d_bh) ;
    return 1 ;
  }
  c_bh = bread(p_s_sb->s_dev, rs->s_journal_block + ((trans_offset + desc->j_len + 1) % JOURNAL_BLOCK_COUNT), 
    		p_s_sb->s_blocksize) ;
  if (!c_bh) {
    brelse(d_bh) ;
    return 1 ;
  }
  commit = (struct reiserfs_journal_commit *)c_bh->b_data ;
  if (journal_compare_desc_commit(p_s_sb, desc, commit)) {
    printk("journal_read_transaction, commit offset %ld had bad time %d or length %d\n", 
	    c_bh->b_blocknr - rs->s_journal_block,commit->j_trans_id, commit->j_len);
    brelse(c_bh) ;
    brelse(d_bh) ;
    return 1; 
  }
  trans_id = desc->j_trans_id ;
  /* now we know we've got a good transaction, and it was inside the valid time ranges */
  log_blocks = kmalloc(desc->j_len * sizeof(struct buffer_head *), GFP_KERNEL) ;
  real_blocks = kmalloc(desc->j_len * sizeof(struct buffer_head *), GFP_KERNEL) ;
  if (!log_blocks  || !real_blocks) {
    brelse(c_bh) ;
    brelse(d_bh) ;
    kfree(log_blocks) ;
    kfree(real_blocks) ;
    reiserfs_warning("journal-1169: kmalloc failed, unable to mount FS\n") ;
    return -1 ;
  }
  /* get all the buffer heads */
  for(i = 0 ; i < desc->j_len ; i++) {
    log_blocks[i] = getblk(p_s_sb->s_dev, rs->s_journal_block + (trans_offset + 1 + i) % JOURNAL_BLOCK_COUNT, p_s_sb->s_blocksize);
    if (i < JOURNAL_TRANS_HALF) {
      real_blocks[i] = getblk(p_s_sb->s_dev, desc->j_realblock[i], p_s_sb->s_blocksize) ;
    } else {
      real_blocks[i] = getblk(p_s_sb->s_dev, commit->j_realblock[i - JOURNAL_TRANS_HALF], p_s_sb->s_blocksize) ;
    }
    if (real_blocks[i]->b_blocknr >= rs->s_journal_block &&
        real_blocks[i]->b_blocknr < (rs->s_journal_block+JOURNAL_BLOCK_COUNT)) {
      reiserfs_warning("journal-1204: REPLAY FAILURE fsck required! Trying to replay onto a log block\n") ;
      brelse_array(log_blocks, i) ;
      brelse_array(real_blocks, i) ;
      brelse(c_bh) ;
      brelse(d_bh) ;
      reiserfs_kfree(log_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      reiserfs_kfree(real_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      return -1 ;
    }
  }
  /* read in the log blocks, memcpy to the corresponding real block */
  ll_rw_block(READ, desc->j_len, log_blocks) ;
  for (i = 0 ; i < desc->j_len ; i++) {
    wait_on_buffer(log_blocks[i]) ;
    if (!buffer_uptodate(log_blocks[i])) {
      reiserfs_warning("journal-1212: REPLAY FAILURE fsck required! buffer write failed\n") ;
      brelse_array(log_blocks + i, desc->j_len - i) ;
      brelse_array(real_blocks, desc->j_len) ;
      brelse(c_bh) ;
      brelse(d_bh) ;
      reiserfs_kfree(log_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      reiserfs_kfree(real_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      return -1 ;
    }
    memcpy(real_blocks[i]->b_data, log_blocks[i]->b_data, real_blocks[i]->b_size) ;
    mark_buffer_uptodate(real_blocks[i], 1) ;
    brelse(log_blocks[i]) ;
  }
  /* flush out the real blocks */
  for (i = 0 ; i < desc->j_len ; i++) {
    set_bit(BH_Dirty, &(real_blocks[i]->b_state)) ;
    ll_rw_block(WRITE, 1, real_blocks + i) ;
  }
  for (i = 0 ; i < desc->j_len ; i++) {
    wait_on_buffer(real_blocks[i]) ; 
    if (!buffer_uptodate(real_blocks[i])) {
      reiserfs_warning("journal-1226: REPLAY FAILURE, fsck required! buffer write failed\n") ;
      brelse_array(real_blocks + i, desc->j_len - i) ;
      brelse(c_bh) ;
      brelse(d_bh) ;
      reiserfs_kfree(log_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      reiserfs_kfree(real_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
      return -1 ;
    }
    brelse(real_blocks[i]) ;
  }
  cur_dblock = rs->s_journal_block + ((trans_offset + desc->j_len + 2) % JOURNAL_BLOCK_COUNT) ;
printk("journal-1095: setting journal start to offset %ld\n", cur_dblock - rs->s_journal_block) ;
  
  /* init starting values for the first transaction, in case this is the last transaction to be replayed. */
  SB_JOURNAL(p_s_sb)->j_start = cur_dblock - rs->s_journal_block ;
  SB_JOURNAL(p_s_sb)->j_last_flush_trans_id = trans_id ;
  SB_JOURNAL(p_s_sb)->j_trans_id = trans_id + 1;
  brelse(c_bh) ;
  brelse(d_bh) ;
  reiserfs_kfree(log_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
  reiserfs_kfree(real_blocks, le32_to_cpu(desc->j_len) * sizeof(struct buffer_head *), p_s_sb) ;
  return 0 ;
}

/*
** read and replay the log
** on a clean unmount, the journal header's next unflushed pointer will be to an invalid
** transaction.  This tests that before finding all the transactions in the log, whic makes normal mount times fast.
**
** After a crash, this starts with the next unflushed transaction, and replays until it finds one too old, or invalid.
**
** On exit, it sets things up so the first transaction will work correctly.
*/
struct buffer_head * reiserfs_breada (kdev_t dev, int block, int bufsize,
			    unsigned int max_block)
{
	struct buffer_head * bhlist[BUFNR];
	unsigned int blocks = BUFNR;
	struct buffer_head * bh;
	int i, j;
	
	bh = getblk (dev, block, bufsize);
	if (buffer_uptodate (bh))
		return (bh);   
		
	if (block + BUFNR > max_block) {
		blocks = max_block - block;
	}
	bhlist[0] = bh;
	j = 1;
	for (i = 1; i < blocks; i++) {
		bh = getblk (dev, block + i, bufsize);
		if (buffer_uptodate (bh)) {
			brelse (bh);
			break;
		}
		else bhlist[j++] = bh;
	}
	ll_rw_block (READ, j, bhlist);
	for(i = 1; i < j; i++) 
		brelse (bhlist[i]);
	bh = bhlist[0];
	wait_on_buffer (bh);
	if (buffer_uptodate (bh))
		return bh;
	brelse (bh);
	return NULL;
}
static int journal_read(struct super_block *p_s_sb) {
  struct reiserfs_super_block *rs = SB_DISK_SUPER_BLOCK(p_s_sb) ;
  struct reiserfs_journal_desc *desc ;
  unsigned long last_flush_trans_id = 0 ;
  unsigned long oldest_trans_id = 0;
  unsigned long oldest_invalid_trans_id = 0 ;
  time_t start ;
  unsigned long last_flush_start = 0;
  unsigned long oldest_start = 0;
  unsigned long cur_dblock = 0 ;
  unsigned long newest_mount_id = 9 ;
  struct buffer_head *d_bh ;
  struct reiserfs_journal_header *jh ;
  int valid_journal_header = 0 ;
  int replay_count = 0 ;
  int continue_replay = 1 ;
  int ret ;

  cur_dblock = rs->s_journal_block ;
printk("journal-1141: reading through journal entries\n") ;
start = CURRENT_TIME ;

  /* step 1, read in the journal header block.  Check the transaction is says is the first unflushed,
  ** and if that transaction is not valid, replay is done
  */
  SB_JOURNAL(p_s_sb)->j_header_bh = bread(p_s_sb->s_dev, rs->s_journal_block + JOURNAL_BLOCK_COUNT, p_s_sb->s_blocksize) ;
  if (!SB_JOURNAL(p_s_sb)->j_header_bh) {
    return 1 ;
  }
  jh = (struct reiserfs_journal_header *)(SB_JOURNAL(p_s_sb)->j_header_bh->b_data) ;
  if (jh->j_first_unflushed_offset >= 0 && jh->j_first_unflushed_offset < JOURNAL_BLOCK_COUNT &&
      jh->j_last_flush_trans_id > 0) {
    last_flush_start = rs->s_journal_block + jh->j_first_unflushed_offset ;
    last_flush_trans_id = jh->j_last_flush_trans_id ;
printk("journal-1153: found in header: first_unflushed_offset %d, last_flushed_trans_id %lu\n", jh->j_first_unflushed_offset, 
										 last_flush_trans_id) ;
    valid_journal_header = 1 ;

    /* now, we try to read the first unflushed offset.  If it is not valid, there is nothing more we can do,
    ** and it makes no sense to read through the whole log.
    */
    d_bh = bread(p_s_sb->s_dev, rs->s_journal_block + jh->j_first_unflushed_offset, p_s_sb->s_blocksize) ;
    ret = journal_transaction_is_valid(p_s_sb, d_bh, NULL, NULL) ;
    if (!ret) {
      continue_replay = 0 ;
    }
    brelse(d_bh) ;
  }

  /* ok, there are transactions that need to be replayed.  start with the first log block, find
  ** all the valid transactions, and pick out the oldest.
  */
  while(continue_replay && cur_dblock < (rs->s_journal_block + JOURNAL_BLOCK_COUNT)) {
    d_bh = bread(p_s_sb->s_dev, cur_dblock, p_s_sb->s_blocksize) ;
    ret = journal_transaction_is_valid(p_s_sb, d_bh, &oldest_invalid_trans_id, &newest_mount_id) ;
    if (ret == 1) {
      desc = (struct reiserfs_journal_desc *)d_bh->b_data ;
      if (oldest_start == 0) { /* init all oldest_ values */
        oldest_trans_id = desc->j_trans_id ;
	oldest_start = d_bh->b_blocknr ;
	newest_mount_id = desc->j_mount_id ;
printk("journal-1179: Setting oldest_start to offset %lu, trans_id %lu\n", oldest_start - rs->s_journal_block, oldest_trans_id) ;
      } else if (oldest_trans_id > desc->j_trans_id) { /* one we just read was older */
        oldest_trans_id = desc->j_trans_id ;
	oldest_start = d_bh->b_blocknr ;
printk("journal-1180: Resetting oldest_start to offset %lu, trans_id %lu\n", 
        oldest_start - rs->s_journal_block, oldest_trans_id) ;
      }
      if (newest_mount_id < desc->j_mount_id) {
        newest_mount_id = desc->j_mount_id ;
printk("journal-1299: Setting newest_mount_id to %d\n", desc->j_mount_id) ;
      }
      cur_dblock += desc->j_len + 2 ;
    } 
    else {
      cur_dblock++ ;
    }
    brelse(d_bh) ;
  }
  /* step three, starting at the oldest transaction, replay */
  if (last_flush_start > 0) {
    oldest_start = last_flush_start ;
    oldest_trans_id = last_flush_trans_id + 1 ;
  } 
  cur_dblock = oldest_start ;
  if (oldest_trans_id) 
    printk("journal-1206: Starting replay from offset %lu, trans_id %lu\n", cur_dblock - rs->s_journal_block, oldest_trans_id) ;

  replay_count = 0 ;
  while(continue_replay && oldest_trans_id > 0) {
    ret = journal_read_transaction(p_s_sb, cur_dblock, oldest_start, oldest_trans_id, newest_mount_id) ;
    if (ret < 0) {
      return ret ;
    } else if (ret != 0) {
      break ;
    }
    cur_dblock = rs->s_journal_block + SB_JOURNAL(p_s_sb)->j_start ;
    replay_count++ ;
   if (cur_dblock == oldest_start)
        break;
  }

  if (oldest_trans_id == 0) {
    printk("journal-1225: No valid transactions found\n") ;
  }
  /* j_start does not get set correctly if we don't replay any transactions.
  ** if we had a valid journal_header, set j_start to the first unflushed transaction value,
  ** copy the trans_id from the header
  */
  if (valid_journal_header && replay_count == 0) { 
    SB_JOURNAL(p_s_sb)->j_start = jh->j_first_unflushed_offset ;
    SB_JOURNAL(p_s_sb)->j_trans_id = jh->j_last_flush_trans_id + 1 ;
    SB_JOURNAL(p_s_sb)->j_last_flush_trans_id = jh->j_last_flush_trans_id ;
    SB_JOURNAL(p_s_sb)->j_mount_id = jh->j_mount_id + 1;
  } else {
    SB_JOURNAL(p_s_sb)->j_mount_id = newest_mount_id + 1 ;
  }
printk("journal-1299: Setting newest_mount_id to %lu\n", SB_JOURNAL(p_s_sb)->j_mount_id) ;
  SB_JOURNAL(p_s_sb)->j_first_unflushed_offset = SB_JOURNAL(p_s_sb)->j_start ; 
  printk("journal-1230: Total replay time: %lu seconds\n", CURRENT_TIME - start) ;
  update_journal_header_block(p_s_sb, SB_JOURNAL(p_s_sb)->j_start, SB_JOURNAL(p_s_sb)->j_last_flush_trans_id) ;
  return 0 ;
}


struct reiserfs_journal_commit_task {
  struct super_block *p_s_sb ;
  int jindex ;
  int wake_on_finish ; /* if this is one, we wake the task_done queue, if it
                       ** is zero, we free the whole struct on finish
		       */
  struct reiserfs_journal_commit_task *self ;
  struct wait_queue *task_done ;
  struct tq_struct task ;
} ;

static void reiserfs_journal_commit_task_func(struct reiserfs_journal_commit_task *ct) {

  flush_commit_list(ct->p_s_sb, SB_JOURNAL_LIST(ct->p_s_sb) + ct->jindex, 1) ; 
  kfree(ct->self) ;
}

static void setup_commit_task_arg(struct reiserfs_journal_commit_task *ct,
                                  struct super_block *p_s_sb, 
				  int jindex) {
  if (!ct) {
    reiserfs_panic(NULL, "journal-1360: setup_commit_task_arg called with NULL struct\n") ;
  }
  ct->p_s_sb = p_s_sb ;
  ct->jindex = jindex ;
  ct->task_done = NULL ;
  ct->task.next = NULL ;
  ct->task.sync = 0 ;
  ct->task.routine = (void *)(void *)reiserfs_journal_commit_task_func ; 
  ct->self = ct ;
  ct->task.data = (void *)ct ;
}

static void commit_flush_async(struct super_block *p_s_sb, int jindex) {
  struct reiserfs_journal_commit_task *ct ;
  /* using GFP_NOFS, GFP_KERNEL could try to flush inodes, which will try
  ** to start/join a transaction, which will deadlock
  */
  ct = reiserfs_kmalloc(sizeof(struct reiserfs_journal_commit_task), GFP_NOFS, p_s_sb) ;
  if (ct) {
    setup_commit_task_arg(ct, p_s_sb, jindex) ;
    queue_task(&(ct->task), &reiserfs_commit_thread_tq);
    wake_up(&reiserfs_commit_thread_wait) ;
  } else {
#ifdef CONFIG_REISERFS_CHECK
    reiserfs_warning("journal-1540: kmalloc failed, doing sync commit\n") ;
#endif
    flush_commit_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + jindex, 1) ;
  }
}

/*
** this is the commit thread.  It is started with kernel_thread on
** FS mount, and journal_release() waits for it to exit.
**
** It could do a periodic commit, but there is a lot code for that
** elsewhere right now, and I only wanted to implement this little
** piece for starters.
**
** All we do here is sleep on the j_commit_thread_wait wait queue, and
** then run the per filesystem commit task queue when we wakeup.
*/
static int reiserfs_journal_commit_thread(void *nullp) {
  int windex ;
  exit_files(current);
  exit_mm(current);

  spin_lock_irq(&current->sigmask_lock);
  sigfillset(&current->blocked);
  recalc_sigpending(current);
  spin_unlock_irq(&current->sigmask_lock);

  current->session = 1;
  current->pgrp = 1;
  sprintf(current->comm, "kreiserfsd") ;
  lock_kernel() ;
  while(1) {
    windex = push_journal_writer("commit thread") ;

    while(reiserfs_commit_thread_tq) {
      run_task_queue(&reiserfs_commit_thread_tq) ;
    }

    /* if there aren't any more filesystems left, break */
    if (reiserfs_mounted_fs_count <= 0) {
      run_task_queue(&reiserfs_commit_thread_tq) ;
      pop_journal_writer(windex) ;
      break ;
    }
    wake_up(&reiserfs_commit_thread_done) ;
    pop_journal_writer(windex) ;
    interruptible_sleep_on_timeout(&reiserfs_commit_thread_wait, 5) ;
  }
  unlock_kernel() ;
  wake_up(&reiserfs_commit_thread_done) ;
  return 0 ;
}

static void journal_list_init(struct super_block *p_s_sb) {
  int i ;
  for (i = 0 ; i < JOURNAL_LIST_COUNT ; i++) {
    init_waitqueue_head(&(SB_JOURNAL_LIST(p_s_sb)[i].j_commit_wait)) ;
    init_waitqueue_head(&(SB_JOURNAL_LIST(p_s_sb)[i].j_flush_wait)) ;
  }
}

/*
** must be called once on fs mount.  calls journal_read for you
*/
int journal_init(struct super_block *p_s_sb) {
  int num_cnodes = JOURNAL_BLOCK_COUNT * 2 ;

  if (sizeof(struct reiserfs_journal_commit) != 4096 ||
      sizeof(struct reiserfs_journal_desc) != 4096
     ) {
    printk("journal-1249: commit or desc struct not 4096 %d %d\n", sizeof(struct reiserfs_journal_commit), 
        sizeof(struct reiserfs_journal_desc)) ;
    return 1 ;
  }
  /* sanity check to make sure they don't overflow the journal */
  if (JOURNAL_BLOCK_COUNT > le32_to_cpu (SB_DISK_SUPER_BLOCK(p_s_sb)->s_orig_journal_size)) {
    printk("journal-1393: current JOURNAL_BLOCK_COUNT (%d) is too big.  This FS was created with a journal size of %d blocks\n",
            JOURNAL_BLOCK_COUNT, le32_to_cpu (SB_DISK_SUPER_BLOCK(p_s_sb)->s_orig_journal_size)) ;
    return 1 ;
  }
  SB_JOURNAL(p_s_sb) = vmalloc(sizeof (struct reiserfs_journal)) ;

  if (!SB_JOURNAL(p_s_sb)) {
    printk("journal-1256: unable to get memory for journal structure\n") ;
    return 1 ;
  }
  memset(SB_JOURNAL(p_s_sb), 0, sizeof(struct reiserfs_journal)) ;

  SB_JOURNAL(p_s_sb)->j_list_bitmap_index = 0 ;
  SB_JOURNAL_LIST_INDEX(p_s_sb) = -10000 ; /* make sure flush_old_commits does not try to flush a list while replay is on */

  /* clear out the journal list array */
  memset(SB_JOURNAL_LIST(p_s_sb), 0, sizeof(struct reiserfs_journal_list) * JOURNAL_LIST_COUNT) ; 
  journal_list_init(p_s_sb) ;

  memset(SB_JOURNAL(p_s_sb)->j_list_hash_table, 0, JOURNAL_HASH_SIZE * sizeof(struct reiserfs_journal_cnode *)) ;
  memset(journal_writers, 0, sizeof(char *) * 512) ; /* debug code */

  allocate_list_bitmaps(p_s_sb) ;
  SB_JOURNAL(p_s_sb)->j_start = 0 ;
  SB_JOURNAL(p_s_sb)->j_len = 0 ;
  SB_JOURNAL(p_s_sb)->j_len_alloc = 0 ;
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_wcount), 0) ;
  SB_JOURNAL(p_s_sb)->j_bcount = 0 ;	  
  SB_JOURNAL(p_s_sb)->j_trans_start_time = 0 ;	  
  SB_JOURNAL(p_s_sb)->j_last = NULL ;	  
  SB_JOURNAL(p_s_sb)->j_first = NULL ;     
  init_waitqueue_head(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
  init_waitqueue_head(&(SB_JOURNAL(p_s_sb)->j_wait)) ; 

  SB_JOURNAL(p_s_sb)->j_trans_id = 10 ;  
  SB_JOURNAL(p_s_sb)->j_mount_id = 10 ; 
  SB_JOURNAL(p_s_sb)->j_state = 0 ;
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_jlock), 0) ;
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_wlock), 0) ;
  SB_JOURNAL(p_s_sb)->j_cnode_free_list = allocate_cnodes(num_cnodes) ;
  SB_JOURNAL(p_s_sb)->j_cnode_free_orig = SB_JOURNAL(p_s_sb)->j_cnode_free_list ;
  SB_JOURNAL(p_s_sb)->j_cnode_free = SB_JOURNAL(p_s_sb)->j_cnode_free_list ? num_cnodes : 0 ;
  SB_JOURNAL(p_s_sb)->j_cnode_used = 0 ;
  SB_JOURNAL(p_s_sb)->j_must_wait = 0 ;
  init_journal_hash(p_s_sb) ;
  SB_JOURNAL_LIST(p_s_sb)[0].j_list_bitmap = get_list_bitmap(p_s_sb, SB_JOURNAL_LIST(p_s_sb)) ;
  if (!(SB_JOURNAL_LIST(p_s_sb)[0].j_list_bitmap)) {
    reiserfs_warning("journal-2005, get_list_bitmap failed for journal list 0\n") ;
    return 1 ;
  }
  if (journal_read(p_s_sb) < 0) {
    reiserfs_warning("Replay Failure, unable to mount\n") ;
    return 1 ;
  }
  SB_JOURNAL_LIST_INDEX(p_s_sb) = 0 ; /* once the read is done, we can set this
                                         where it belongs */

  INIT_LIST_HEAD (&SB_JOURNAL(p_s_sb)->j_prealloc_list);

  if (reiserfs_dont_log (p_s_sb))
    return 0;

  reiserfs_mounted_fs_count++ ;
  if (reiserfs_mounted_fs_count <= 1) {
    kernel_thread((void *)(void *)reiserfs_journal_commit_thread, NULL,
                  CLONE_FS | CLONE_FILES | CLONE_VM) ;
  }
  return 0 ;
}

/*
** test for a polite end of the current transaction.  Used by file_write, and should
** be used by delete to make sure they don't write more than can fit inside a single
** transaction
*/
int journal_transaction_should_end(struct reiserfs_transaction_handle *th, int new_alloc) {
  time_t now = CURRENT_TIME ;
  if (reiserfs_dont_log(th->t_super)) 
    return 0 ;
  if ( SB_JOURNAL(th->t_super)->j_must_wait > 0 ||
       (SB_JOURNAL(th->t_super)->j_len_alloc + new_alloc) >= JOURNAL_MAX_BATCH || 
       atomic_read(&(SB_JOURNAL(th->t_super)->j_jlock)) ||
      (now - SB_JOURNAL(th->t_super)->j_trans_start_time) > JOURNAL_MAX_TRANS_AGE ||
       SB_JOURNAL(th->t_super)->j_cnode_free < (JOURNAL_TRANS_MAX * 3)) { 
    return 1 ;
  }
  return 0 ;
}

/* join == true if you must join an existing transaction.
** join == false if you can deal with waiting for others to finish
**
** this will block until the transaction is joinable.  send the number of blocks you
** expect to use in nblocks.
*/
static int do_journal_begin_r(struct reiserfs_transaction_handle *th, struct super_block * p_s_sb,unsigned long nblocks,int join) {
  time_t now = CURRENT_TIME ;
  int windex  ;
  int old_trans_id  ;
  /*   int free_some_ram = 0 ; -Hans */

  reiserfs_check_lock_depth("journal_begin") ;

  if (reiserfs_dont_log(p_s_sb)) {
    th->t_super = p_s_sb ; /* others will check this for the don't log flag */
    return 0 ;
  }

  /* we never want to make kswapd wait, never, ever.  So, the only time
  ** we make it wait is when joining the transaction would result in
  ** overflow.
  **
  ** ugly, nasty stuff, we need real callbacks from the VFS layer to do
  ** this right.
  */
  if (!strcmp(current->comm, "kswapd")) {
    /*
    SB_JOURNAL(p_s_sb)->j_must_wait = 1 ;
    flush_async_commits(p_s_sb) ;
    */
    if ((SB_JOURNAL(p_s_sb)->j_len_alloc + nblocks + 2) < JOURNAL_MAX_BATCH) {
      join = 1 ;
    }
  }

  lock_journal(p_s_sb) ;

  /* if there is no room in the journal OR
  ** if this transaction is too old, and we weren't called joinable, wait for it to finish before beginning 
  ** we don't sleep if there aren't other writers
  */

  if (  (!join && SB_JOURNAL(p_s_sb)->j_must_wait > 0) ||
     ( !join && (SB_JOURNAL(p_s_sb)->j_len_alloc + nblocks + 2) >= JOURNAL_MAX_BATCH) || 
     (!join && atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) > 0 && SB_JOURNAL(p_s_sb)->j_trans_start_time > 0 && 
      (now - SB_JOURNAL(p_s_sb)->j_trans_start_time) > JOURNAL_MAX_TRANS_AGE) ||
     (!join && atomic_read(&(SB_JOURNAL(p_s_sb)->j_jlock)) ) ||
     (!join && SB_JOURNAL(p_s_sb)->j_cnode_free < (JOURNAL_TRANS_MAX * 3))) {

    unlock_journal(p_s_sb) ; /* allow others to finish this transaction */

    /* if writer count is 0, we can just force this transaction to end, and start
    ** a new one afterwards.
    */
    if (atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) <= 0) {
      struct reiserfs_transaction_handle myth ;
      journal_join(&myth, p_s_sb, 1) ;
      windex = push_journal_writer("journal_begin") ;
      reiserfs_prepare_for_journal(p_s_sb, SB_BUFFER_WITH_SB(p_s_sb), 1) ;
      journal_mark_dirty(&myth, p_s_sb, SB_BUFFER_WITH_SB(p_s_sb)) ;
      pop_journal_writer(windex) ;
      do_journal_end(&myth, p_s_sb,1,COMMIT_NOW) ;
    } else {
      /* but if the writer count isn't zero, we have to wait for the current writers to finish.
      ** They won't batch on transaction end once we set j_jlock
      */
      atomic_set(&(SB_JOURNAL(p_s_sb)->j_jlock), 1) ;
      old_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;
      while(atomic_read(&(SB_JOURNAL(p_s_sb)->j_jlock)) &&
            SB_JOURNAL(p_s_sb)->j_trans_id == old_trans_id) {
	sleep_on(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
      }
    }
    goto relock ;
  }

  if (SB_JOURNAL(p_s_sb)->j_trans_start_time == 0) { /* we are the first writer, set trans_id */
    SB_JOURNAL(p_s_sb)->j_trans_start_time = now ;
  }
  atomic_inc(&(SB_JOURNAL(p_s_sb)->j_wcount)) ;
  SB_JOURNAL(p_s_sb)->j_len_alloc += nblocks ;
  th->t_blocks_logged = 0 ;
  th->t_blocks_allocated = nblocks ;
  th->t_super = p_s_sb ;
  th->t_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;
  th->t_caller = "Unknown" ;
  unlock_journal(p_s_sb) ;
  p_s_sb->s_dirt = 1; 
  return 0 ;
}


static int journal_join(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, unsigned long nblocks) {
  return do_journal_begin_r(th, p_s_sb, nblocks, 1) ;
}

int journal_begin(struct reiserfs_transaction_handle *th, struct super_block  * p_s_sb, unsigned long nblocks) {
  return do_journal_begin_r(th, p_s_sb, nblocks, 0) ;
}

/* not used at all */
int journal_prepare(struct super_block  * p_s_sb, struct buffer_head *bh) {
  return 0 ;
}

/*
** puts bh into the current transaction.  If it was already there, reorders removes the
** old pointers from the hash, and puts new ones in (to make sure replay happen in the right order).
**
** if it was dirty, cleans and files onto the clean list.  I can't let it be dirty again until the
** transaction is committed.
** 
** if j_len, is bigger than j_len_alloc, it pushes j_len_alloc to 10 + j_len.
*/
int journal_mark_dirty(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, struct buffer_head *bh) {
  struct reiserfs_journal_cnode *cn = NULL;
  int count_already_incd = 0 ;
  int windex ;
  int prepared = 0 ;

  if (reiserfs_dont_log(th->t_super)) {
    mark_buffer_dirty(bh, 1) ;
    return 0 ;
  }

  windex = push_journal_writer("journal_mark_dirty") ;
  if (th->t_trans_id != SB_JOURNAL(p_s_sb)->j_trans_id) {
    reiserfs_panic(th->t_super, "journal-1577: handle trans id %d != current trans id %d\n", 
                   th->t_trans_id, SB_JOURNAL(p_s_sb)->j_trans_id);
  }
  p_s_sb->s_dirt = 1 ;

  prepared = test_and_clear_bit(BH_JPrepared, &bh->b_state) ;
  /* already in this transaction, we are done */
  if (buffer_journaled(bh)) {
    pop_journal_writer(windex) ;
    return 0 ;
  }

#if 0
  /* this must be turned into a panic instead of a warning.  We can't allow
  ** a dirty or journal_dirty or locked buffer to be logged, as some changes
  ** could get to disk too early.  NOT GOOD.
  */
  if (!prepared || buffer_locked(bh) || buffer_dirty(bh)) {
    printk("journal-1777: buffer %lu bad state %cPREPARED %cLOCKED %cDIRTY %cJDIRTY_WAIT\n", bh->b_blocknr, prepared ? ' ' : '!', 
                            buffer_locked(bh) ? ' ' : '!',
			    buffer_dirty(bh) ? ' ' : '!',
			    buffer_journal_dirty(bh) ? ' ' : '!') ;
    show_reiserfs_locks() ;
  }
#endif
  count_already_incd = clear_prepared_bits(bh) ;

  if (atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) <= 0) {
    printk("journal-1409: journal_mark_dirty returning because j_wcount was %d\n", atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount))) ;
    pop_journal_writer(windex) ;
    return 1 ;
  }
  /* this error means I've screwed up, and we've overflowed the transaction.  
  ** Nothing can be done here, except make the FS readonly or panic.
  */ 
  if (SB_JOURNAL(p_s_sb)->j_len >= JOURNAL_TRANS_MAX) { 
    reiserfs_panic(th->t_super, "journal-1413: journal_mark_dirty: j_len (%lu) is too big\n", SB_JOURNAL(p_s_sb)->j_len) ;
  }

  if (buffer_journal_dirty(bh)) {
    count_already_incd = 1 ;
    mark_buffer_notjournal_dirty(bh) ;
  }

  if (buffer_dirty(bh)) {
    clear_bit(BH_Dirty, &bh->b_state) ;
  }

  if (buffer_journaled(bh)) { /* must double check after getting lock */
    goto done ;
  }

  if (SB_JOURNAL(p_s_sb)->j_len > SB_JOURNAL(p_s_sb)->j_len_alloc) {
    SB_JOURNAL(p_s_sb)->j_len_alloc = SB_JOURNAL(p_s_sb)->j_len + JOURNAL_PER_BALANCE_CNT ;
  }

  set_bit(BH_JDirty, &bh->b_state) ;

  /* now put this guy on the end */
  if (!cn) {
    cn = get_cnode(p_s_sb) ;
    if (!cn) {
      reiserfs_panic(p_s_sb, "get_cnode failed!\n"); 
    }

    if (th->t_blocks_logged == th->t_blocks_allocated) {
      th->t_blocks_allocated += JOURNAL_PER_BALANCE_CNT ;
      SB_JOURNAL(p_s_sb)->j_len_alloc += JOURNAL_PER_BALANCE_CNT ;
    }
    th->t_blocks_logged++ ;
    SB_JOURNAL(p_s_sb)->j_len++ ;

    cn->bh = bh ;
    cn->blocknr = bh->b_blocknr ;
    cn->dev = bh->b_dev ;
    cn->jlist = NULL ;
    insert_journal_hash(SB_JOURNAL(p_s_sb)->j_hash_table, cn) ;
    if (!count_already_incd) {
      get_bh(bh) ;
    }
  }
  cn->next = NULL ;
  cn->prev = SB_JOURNAL(p_s_sb)->j_last ;
  cn->bh = bh ;
  if (SB_JOURNAL(p_s_sb)->j_last) {
    SB_JOURNAL(p_s_sb)->j_last->next = cn ;
    SB_JOURNAL(p_s_sb)->j_last = cn ;
  } else {
    SB_JOURNAL(p_s_sb)->j_first = cn ;
    SB_JOURNAL(p_s_sb)->j_last = cn ;
  }
done:
  pop_journal_writer(windex) ;
  return 0 ;
}

/*
** if buffer already in current transaction, do a journal_mark_dirty
** otherwise, just mark it dirty and move on.  Used for writes to meta blocks
** that don't need journaling
*/
int journal_mark_dirty_nolog(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, struct buffer_head *bh) {
  int windex = push_journal_writer("dirty_nolog") ;
  if (reiserfs_dont_log(th->t_super) || buffer_journaled(bh) || 
      buffer_journal_dirty(bh)) {
    pop_journal_writer(windex) ;
    return journal_mark_dirty(th, p_s_sb, bh) ;
  }
  if (get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_list_hash_table, bh->b_dev,bh->b_blocknr,bh->b_size)) {
    pop_journal_writer(windex) ;
    return journal_mark_dirty(th, p_s_sb, bh) ;
  }
  mark_buffer_dirty(bh , 0) ;
  pop_journal_writer(windex) ;
  return 0 ;
}

int journal_end(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, unsigned long nblocks) {
  return do_journal_end(th, p_s_sb, nblocks, 0) ;
}

/* removes from the current transaction, relsing and descrementing any counters.  
** also files the removed buffer directly onto the clean list
**
** called by journal_mark_freed when a block has been deleted
**
** returns 1 if it cleaned and relsed the buffer. 0 otherwise
*/
static int remove_from_transaction(struct super_block *p_s_sb, unsigned long blocknr, int already_cleaned) {
  struct buffer_head *bh ;
  struct reiserfs_journal_cnode *cn ;
  int ret = 0;

  cn = get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_hash_table, p_s_sb->s_dev, blocknr, p_s_sb->s_blocksize) ;
  if (!cn || !cn->bh) {
    return ret ;
  }
  bh = cn->bh ;
  if (cn->prev) {
    cn->prev->next = cn->next ;
  }
  if (cn->next) {
    cn->next->prev = cn->prev ;
  }
  if (cn == SB_JOURNAL(p_s_sb)->j_first) {
    SB_JOURNAL(p_s_sb)->j_first = cn->next ;  
  }
  if (cn == SB_JOURNAL(p_s_sb)->j_last) {
    SB_JOURNAL(p_s_sb)->j_last = cn->prev ;
  }
  remove_journal_hash(SB_JOURNAL(p_s_sb)->j_hash_table, NULL, bh, 0) ; 
  mark_buffer_not_journaled(bh) ; /* don't log this one */

  if (!already_cleaned) {
    mark_buffer_notjournal_dirty(bh) ; 
    put_bh(bh) ;
    if (atomic_read(&(bh->b_count)) < 0) {
      printk("journal-1752: remove from trans, b_count < 0\n") ;
    }
    if (!buffer_locked(bh)) reiserfs_clean_and_file_buffer(bh) ; 
    ret = 1 ;
  }
  SB_JOURNAL(p_s_sb)->j_len-- ;
  SB_JOURNAL(p_s_sb)->j_len_alloc-- ;
  free_cnode(p_s_sb, cn) ;
  return ret ;
}

/* removes from a specific journal list hash */
static int remove_from_journal_list(struct super_block *s, struct reiserfs_journal_list *jl, struct buffer_head *bh, int remove_freed) {
  remove_journal_hash(SB_JOURNAL(s)->j_list_hash_table, jl, bh, remove_freed) ;
  return 0 ;
}

/*
** for any cnode in a journal list, it can only be dirtied of all the
** transactions that include it are commited to disk.
** this checks through each transaction, and returns 1 if you are allowed to dirty,
** and 0 if you aren't
**
** it is called by dirty_journal_list, which is called after flush_commit_list has gotten all the log
** blocks for a given transaction on disk
**
*/
static int can_dirty(struct reiserfs_journal_cnode *cn) {
  kdev_t dev = cn->dev ;
  unsigned long blocknr = cn->blocknr  ;
  struct reiserfs_journal_cnode *cur = cn->hprev ;
  int can_dirty = 1 ;
  
  /* first test backwards */
  while(cur && can_dirty) {
    if (cur->jlist && cur->jlist->j_len > 0 && atomic_read(&(cur->jlist->j_commit_left)) > 0 && cur->bh && cur->blocknr && 
        cur->dev == dev && cur->blocknr == blocknr 
    ) {
      can_dirty = 0 ;
    }
    cur = cur->hprev ;
  }
  /* then test fowards */
  cur = cn->hnext ;
  while(cur && can_dirty) {
    if (cur->jlist && cur->jlist->j_len > 0 && atomic_read(&(cur->jlist->j_commit_left)) > 0 && cur->bh && cur->blocknr && 
        cur->dev == dev && cur->blocknr == blocknr 
    ) {
      can_dirty = 0 ;
    }
    cur = cur->hnext ;
  }
  return can_dirty ;
}

/*
** Whereever possible, this dirties and releases the real blocks associated 
** with a transaction
** 
** called by flush_commit_list, after all the log blocks for a transaction 
** are on disk.  
*/
static void dirty_journal_list(struct super_block *p_s_sb, struct reiserfs_journal_list *jl) {
  struct buffer_head *tbh ;
  struct reiserfs_journal_cnode *cn ;
  int dirty_it  ;
  int windex = push_journal_writer("dirty_journal_list") ;

  cn = jl->j_realblock ;

  while(cn) {
    /* remove_from_journal_list invalidates the bh in j_realblock, must 
    ** copy it first */
    tbh = cn->bh ;

    /* default to dirty the block */
    dirty_it = 1 ;
    if (cn->blocknr && tbh) {
      /* we only want to dirty the block if all the log blocks in all 
         his transactions are on disk */
      dirty_it = can_dirty(cn) ;
      if (dirty_it) {
	if (buffer_journal_dirty(tbh)) {
	  if (test_bit(BH_JPrepared, &tbh->b_state)) {
	    set_bit(BH_JRestore_dirty, &tbh->b_state) ;
	  } else {
	    mark_buffer_dirty(tbh, 0) ;
	  }
	} 
      }
    }
    cn = cn->next ;
  }
  pop_journal_writer(windex) ;
}

/* syncs the commit blocks, but does not force the real buffers to disk
** will wait until the current transaction is done/commited before returning 
*/
int journal_end_sync(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, unsigned long nblocks) {

  if (SB_JOURNAL(p_s_sb)->j_len == 0) {
    reiserfs_prepare_for_journal(p_s_sb, SB_BUFFER_WITH_SB(p_s_sb), 1) ;
    journal_mark_dirty(th, p_s_sb, SB_BUFFER_WITH_SB(p_s_sb)) ;
  }
  return do_journal_end(th, p_s_sb, nblocks, COMMIT_NOW | WAIT) ;
}

int show_reiserfs_locks(void) {

  dump_journal_writers() ;
  return 0 ;
}

/*
** used to get memory back from async commits that are floating around
** and to reclaim any blocks deleted but unusable because their commits
** haven't hit disk yet.  called from bitmap.c
**
** if it starts flushing things, it ors SCHEDULE_OCCURRED into repeat.
** note, this is just if schedule has a chance of occuring.  I need to 
** change flush_commit_lists to have a repeat parameter too.
**
*/
void flush_async_commits(struct super_block *p_s_sb) {
  int i ;
  int windex ;

  windex = push_journal_writer("flush_async_commits") ;
  for (i = 0 ; i < JOURNAL_LIST_COUNT ; i++) {
    if (i != SB_JOURNAL_LIST_INDEX(p_s_sb)) {
      flush_commit_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + i, 1) ; 
    }
  }
  pop_journal_writer(windex) ;
}

/*
** flushes any old transactions to disk
** ends the current transaction if it is too old
**
** also calls flush_journal_list with old_only == 1, which allows me to reclaim
** memory and such from the journal lists whose real blocks are all on disk.
**
** called by sync_dev_journal from buffer.c
*/
int flush_old_commits(struct super_block *p_s_sb, int immediate) {
  int i ;
  int count = 0;
  int start ; 
  time_t now ; 
  int windex ;
  int keep_dirty = 0 ;
  struct reiserfs_transaction_handle th ; 

  start =  SB_JOURNAL_LIST_INDEX(p_s_sb) ;
  now = CURRENT_TIME ;

  /* safety check so we don't flush while we are replaying the log during mount */
  if (SB_JOURNAL_LIST_INDEX(p_s_sb) < 0) {
    return 0  ;
  }
  /* starting with oldest, loop until we get to the start */
  i = (SB_JOURNAL_LIST_INDEX(p_s_sb) + 1) % JOURNAL_LIST_COUNT ;
  while(i != start) {
    if (SB_JOURNAL_LIST(p_s_sb)[i].j_len > 0 && ((now - SB_JOURNAL_LIST(p_s_sb)[i].j_timestamp) > JOURNAL_MAX_COMMIT_AGE ||
       immediate)) {
      /* we have to check again to be sure the current transaction did not change */
      if (i != SB_JOURNAL_LIST_INDEX(p_s_sb))  {
	flush_commit_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + i, 1) ;
      }
    }
    /* now we free ram used by the old journal lists */
    if (SB_JOURNAL_LIST(p_s_sb)[i].j_len > 0 && i != SB_JOURNAL_LIST_INDEX(p_s_sb))  {
      flush_journal_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + i, 1, 0) ; /* old_only, and don't flush all, 
      					                                 we only want to reclaim nodes if it will be fast */
    }
    i = (i + 1) % JOURNAL_LIST_COUNT ;
    count++ ;
  }
  /* now, check the current transaction.  If there are no writers, and it is too old, finish it, and
  ** force the commit blocks to disk
  */
  if (!immediate && atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) <= 0 &&  
     SB_JOURNAL(p_s_sb)->j_trans_start_time > 0 && 
     SB_JOURNAL(p_s_sb)->j_len > 0 && 
     (now - SB_JOURNAL(p_s_sb)->j_trans_start_time) > JOURNAL_MAX_TRANS_AGE) {
/*
printk("journal-1743: current trans is too old (%lu seconds)...flushing (immediate was %d)\n", 
        now - SB_JOURNAL(p_s_sb)->j_trans_start_time, immediate) ;
*/
    journal_join(&th, p_s_sb, 1) ;
    windex = push_journal_writer("flush_old_commits") ;
    reiserfs_prepare_for_journal(p_s_sb, SB_BUFFER_WITH_SB(p_s_sb), 1) ;
    journal_mark_dirty(&th, p_s_sb, SB_BUFFER_WITH_SB(p_s_sb)) ;
    pop_journal_writer(windex) ;
    do_journal_end(&th, p_s_sb,1, COMMIT_NOW) ;
  } else if (immediate) { /* belongs above, but I wanted this to be very explicit as a special case.  If they say to 
                             flush, we must be sure old transactions hit the disk too. */
    journal_join(&th, p_s_sb, 1) ;
    reiserfs_prepare_for_journal(p_s_sb, SB_BUFFER_WITH_SB(p_s_sb), 1) ;
    journal_mark_dirty(&th, p_s_sb, SB_BUFFER_WITH_SB(p_s_sb)) ;
    do_journal_end(&th, p_s_sb,1, COMMIT_NOW | WAIT) ;
  }
  return keep_dirty ;
}

/*
** returns 0 if do_journal_end should return right away, returns 1 if do_journal_end should finish the commit
** 
** if the current transaction is too old, but still has writers, this will wait on j_join_wait until all 
** the writers are done.  By the time it wakes up, the transaction it was called has already ended, so it just
** flushes the commit list and returns 0.
**
** Won't batch when flush or commit_now is set.  Also won't batch when others are waiting on j_join_wait.
** 
** Note, we can't allow the journal_end to proceed while there are still writers in the log.
*/
static int check_journal_end(struct reiserfs_transaction_handle *th, struct super_block  * p_s_sb, 
                             unsigned long nblocks, int flags) {

  time_t now ;
  int flush = flags & FLUSH_ALL ;
  int commit_now = flags & COMMIT_NOW ;
  int wait_on_commit = flags & WAIT ;

  if (th->t_trans_id != SB_JOURNAL(p_s_sb)->j_trans_id) {
    reiserfs_panic(th->t_super, "journal-1577: handle trans id %ld != current trans id %ld\n", 
                   th->t_trans_id, SB_JOURNAL(p_s_sb)->j_trans_id);
  }
  PROC_INFO_INC( p_s_sb, journal.journal_being );

  SB_JOURNAL(p_s_sb)->j_len_alloc -= (th->t_blocks_allocated - th->t_blocks_logged) ;
  if (atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) > 0) { /* <= 0 is allowed.  unmounting might not call begin */
    atomic_dec(&(SB_JOURNAL(p_s_sb)->j_wcount)) ;
  }

  /* BUG, deal with case where j_len is 0, but people previously freed blocks need to be released 
  ** will be dealt with by next transaction that actually writes something, but should be taken
  ** care of in this trans
  */
  if (SB_JOURNAL(p_s_sb)->j_len == 0) {
    int wcount = atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) ;
    unlock_journal(p_s_sb) ;
    if (atomic_read(&(SB_JOURNAL(p_s_sb)->j_jlock))  > 0 && wcount <= 0) {
      atomic_dec(&(SB_JOURNAL(p_s_sb)->j_jlock)) ;
      wake_up(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
    }
    return 0 ;
  }
  /* if wcount > 0, and we are called to with flush or commit_now,
  ** we wait on j_join_wait.  We will wake up when the last writer has
  ** finished the transaction, and started it on its way to the disk.
  ** Then, we flush the commit or journal list, and just return 0 
  ** because the rest of journal end was already done for this transaction.
  */
  if (atomic_read(&(SB_JOURNAL(p_s_sb)->j_wcount)) > 0) {
    if (flush || commit_now) {
      int orig_jindex = SB_JOURNAL_LIST_INDEX(p_s_sb) ;
      atomic_set(&(SB_JOURNAL(p_s_sb)->j_jlock), 1) ;
      if (flush) {
        SB_JOURNAL(p_s_sb)->j_next_full_flush = 1 ;
      }
      unlock_journal(p_s_sb) ;
      /* sleep while the current transaction is still j_jlocked */
      while(atomic_read(&(SB_JOURNAL(p_s_sb)->j_jlock)) && 
            SB_JOURNAL(p_s_sb)->j_trans_id == th->t_trans_id) {
	sleep_on(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
      }
      if (commit_now) {
	if (wait_on_commit) {
	  flush_commit_list(p_s_sb,  SB_JOURNAL_LIST(p_s_sb) + orig_jindex, 1) ;
	} else {
	  commit_flush_async(p_s_sb, orig_jindex) ; 
	}
      }
      return 0 ;
    } 
    unlock_journal(p_s_sb) ;
    return 0 ;
  }

  /* deal with old transactions where we are the last writers */
  now = CURRENT_TIME ;
  if ((now - SB_JOURNAL(p_s_sb)->j_trans_start_time) > JOURNAL_MAX_TRANS_AGE) {
    commit_now = 1 ;
    SB_JOURNAL(p_s_sb)->j_next_async_flush = 1 ;
  }
  /* don't batch when someone is waiting on j_join_wait */
  /* don't batch when syncing the commit or flushing the whole trans */
  if (!(SB_JOURNAL(p_s_sb)->j_must_wait > 0) && !(atomic_read(&(SB_JOURNAL(p_s_sb)->j_jlock))) && !flush && !commit_now && 
      (SB_JOURNAL(p_s_sb)->j_len < JOURNAL_MAX_BATCH)  && 
      SB_JOURNAL(p_s_sb)->j_len_alloc < JOURNAL_MAX_BATCH && SB_JOURNAL(p_s_sb)->j_cnode_free > (JOURNAL_TRANS_MAX * 3)) {
    SB_JOURNAL(p_s_sb)->j_bcount++ ;
    unlock_journal(p_s_sb) ;
    return 0 ;
  }

  if (SB_JOURNAL(p_s_sb)->j_start > JOURNAL_BLOCK_COUNT) {
    reiserfs_panic(p_s_sb, "journal-003: journal_end: j_start (%ld) is too high\n", SB_JOURNAL(p_s_sb)->j_start) ;
  }
  return 1 ;
}

/*
** Does all the work that makes deleting blocks safe.
** when deleting a block mark BH_JNew, just remove it from the current transaction, clean it's buffer_head and move on.
** 
** otherwise:
** set a bit for the block in the journal bitmap.  That will prevent it from being allocated for unformatted nodes
** before this transaction has finished.
**
** mark any cnodes for this block as BLOCK_FREED, and clear their bh pointers.  That will prevent any old transactions with
** this block from trying to flush to the real location.  Since we aren't removing the cnode from the journal_list_hash,
** the block can't be reallocated yet.
**
** Then remove it from the current transaction, decrementing any counters and filing it on the clean list.
*/
int journal_mark_freed(struct reiserfs_transaction_handle *th, struct super_block *p_s_sb, unsigned long blocknr) {
  struct reiserfs_journal_cnode *cn = NULL ;
  struct buffer_head *bh = NULL ;
  struct reiserfs_list_bitmap *jb = NULL ;
  int cleaned = 0 ;
  int windex = push_journal_writer("journal_mark_freed") ;
  
  if (reiserfs_dont_log(th->t_super)) {
    bh = sb_get_hash_table(p_s_sb, blocknr) ;
    if (bh && buffer_dirty (bh)) {
      printk ("journal_mark_freed(dont_log): dirty buffer on hash list: %lx %ld\n", bh->b_state, blocknr);
      BUG ();
    }
    brelse (bh);
    pop_journal_writer(windex) ;
    return 0 ;
  }
  bh = sb_get_hash_table(p_s_sb, blocknr) ;
  /* if it is journal new, we just remove it from this transaction */
  if (bh && buffer_journal_new(bh)) {
    mark_buffer_notjournal_new(bh) ;
    clear_prepared_bits(bh) ;
    cleaned = remove_from_transaction(p_s_sb, blocknr, cleaned) ;
  } else {
    /* set the bit for this block in the journal bitmap for this transaction */
    jb = SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_list_bitmap ;
    if (!jb) {
      reiserfs_panic(p_s_sb, "journal-1702: journal_mark_freed, journal_list_bitmap is NULL\n") ;
    }
    set_bit(blocknr, jb->bitmap) ;

    /* Note, the entire while loop is not allowed to schedule.  */

    if (bh) {
      clear_prepared_bits(bh) ;
    }
    cleaned = remove_from_transaction(p_s_sb, blocknr, cleaned) ;

    /* find all older transactions with this block, make sure they don't try to write it out */
    cn = get_journal_hash_dev(SB_JOURNAL(p_s_sb)->j_list_hash_table, p_s_sb->s_dev, blocknr, p_s_sb->s_blocksize) ;
    while (cn) {
      if (p_s_sb->s_dev == cn->dev && blocknr == cn->blocknr) {
	set_bit(BLOCK_FREED, &cn->state) ;
	if (cn->bh) {
	  if (!cleaned) {
	    /* remove_from_transaction will brelse the buffer if it was 
	    ** in the current trans
	    */
	    mark_buffer_notjournal_dirty(cn->bh) ;
	    cleaned = 1 ;
	    put_bh(cn->bh) ;
	    if (atomic_read(&(cn->bh->b_count)) < 0) {
	      printk("journal-2138: cn->bh->b_count < 0\n") ;
	    }
	  }
	  if (cn->jlist) { /* since we are clearing the bh, we MUST dec nonzerolen */
	    atomic_dec(&(cn->jlist->j_nonzerolen)) ;
	  }
	  cn->bh = NULL ; 
	} 
      }
      cn = cn->hnext ;
    }
  }

  if (bh) {
    reiserfs_clean_and_file_buffer(bh) ;
    put_bh(bh) ; /* get_hash grabs the buffer */
    if (atomic_read(&(bh->b_count)) < 0) {
      printk("journal-2165: bh->b_count < 0\n") ;
    }
  }
  pop_journal_writer(windex) ;
  return 0 ;
}

void reiserfs_update_inode_transaction(struct inode *inode) {
  
  inode->u.reiserfs_i.i_trans_index = SB_JOURNAL_LIST_INDEX(inode->i_sb);

  inode->u.reiserfs_i.i_trans_id = SB_JOURNAL(inode->i_sb)->j_trans_id ;
}

static int reiserfs_inode_in_this_transaction(struct inode *inode) {
  if (inode->u.reiserfs_i.i_trans_id == SB_JOURNAL(inode->i_sb)->j_trans_id || 
      inode->u.reiserfs_i.i_trans_id == 0) {
    return 1; 
  } 
  return 0 ;
}

void reiserfs_commit_for_inode(struct inode *inode) {
  struct reiserfs_journal_list *jl ;
  struct reiserfs_transaction_handle th ;
  struct super_block *sb = inode->i_sb ;

  jl = SB_JOURNAL_LIST(sb) + inode->u.reiserfs_i.i_trans_index ;

  /* is it from the current transaction, or from an unknown transaction? */
  if (reiserfs_inode_in_this_transaction(inode)) {
    journal_join(&th, sb, 1) ;
    reiserfs_update_inode_transaction(inode) ;
    journal_end_sync(&th, sb, 1) ;
  } else if (jl->j_trans_id == inode->u.reiserfs_i.i_trans_id) {
    flush_commit_list(sb, jl, 1) ;
  }
  /* if the transaction id does not match, this list is long since flushed
  ** and we don't have to do anything here
  */
}

void reiserfs_restore_prepared_buffer(struct super_block *p_s_sb, 
                                      struct buffer_head *bh) {
  if (reiserfs_dont_log (p_s_sb))
    return;

  if (!bh) {
    return ;
  }
  if (test_and_clear_bit(BH_JPrepared, &bh->b_state)) {
    if (test_and_clear_bit(BH_JRestore_dirty, &bh->b_state)) {
      if (!buffer_journaled(bh)) {
	mark_buffer_dirty(bh, 0) ;
      }
    } 
  } 
}

extern struct tree_balance *cur_tb ;
/*
** before we can change a metadata block, we have to make sure it won't
** be written to disk while we are altering it.  So, we must:
** clean it
** wait on it.
** 
*/
void reiserfs_prepare_for_journal(struct super_block *p_s_sb, 
                                  struct buffer_head *bh, int wait) {
  int windex ;
  int retry_count = 0 ;

  if (reiserfs_dont_log (p_s_sb))
    return;

  windex = push_journal_writer("prepare_for_journal") ;
  while(!test_bit(BH_JPrepared, &bh->b_state) ||
        (wait && buffer_locked(bh))) {
    if (buffer_journaled(bh)) {
      set_bit(BH_JPrepared, &bh->b_state) ;
      pop_journal_writer(windex) ;
      return ;
    }
    if (retry_count > 1) {
printk("prepare_for_journal, retry count is %d\n", retry_count) ;
    }
    set_bit(BH_JPrepared, &bh->b_state) ;
    if (test_and_clear_bit(BH_Dirty, &bh->b_state)) {
      set_bit(BH_JRestore_dirty, &bh->b_state) ;
    }
    if (wait) {
#ifdef CONFIG_REISERFS_CHECK
      if (buffer_locked(bh) && cur_tb != NULL) {
	char *crashnow = NULL ;
printk("reiserfs_prepare_for_journal, waiting while do_balance was running\n") ;
	*crashnow = 1 ;  
      }
#endif
      wait_on_buffer(bh) ;
    }
    retry_count++ ;
  }
  pop_journal_writer(windex) ;
}

/*
** This can only be called from do_journal_end.
** it runs through the list things that need flushing before the
** transaction can commit, and writes each of them to disk
**
*/

static void flush_pages_before_commit(struct reiserfs_transaction_handle *th,
                                      struct super_block *p_s_sb) {
  struct reiserfs_page_list *pl = SB_JOURNAL(p_s_sb)->j_flush_pages ;
  struct reiserfs_page_list *pl_tmp ;
  struct buffer_head *bh, *head ;
  int count = 0 ;

  /* first write each dirty unlocked buffer in the list */

  while(pl) {
    /* ugly.  journal_end can be called from get_block, which has a 
    ** page locked.  So, we have to check to see if pl->page is the page
    ** currently locked by the calling function, and if so, skip the
    ** lock
    */
    if (!pl->do_not_lock) {
      lock_page(pl->page) ;
    }
    if (!PageLocked(pl->page)) {
      BUG() ;
    }
    if (pl->page->buffers) {
      head = pl->page->buffers ;
      bh = head ;
      do {
	if (bh->b_blocknr == pl->blocknr && buffer_dirty(bh) &&
	    !buffer_locked(bh) && buffer_uptodate(bh) ) {
	  ll_rw_block(WRITE, 1, &bh) ;
	}
	bh = bh->b_this_page ;
      } while (bh != head) ;
    }
    if (!pl->do_not_lock) {
      UnlockPage(pl->page) ;
    }
    pl = pl->next ;
  }

  /* now wait on them */

  pl = SB_JOURNAL(p_s_sb)->j_flush_pages ;
  while(pl) {
    if (!pl->do_not_lock) {
      lock_page(pl->page) ;
    }
    if (!PageLocked(pl->page)) {
      BUG() ;
    }
    if (pl->page->buffers) {
      head = pl->page->buffers ;
      bh = head ;
      do {
	if (bh->b_blocknr == pl->blocknr) {
	  count++ ;
	  wait_on_buffer(bh) ;
	  if (!buffer_uptodate(bh)) {
	    reiserfs_panic(p_s_sb, "journal-2443: flush_pages_before_commit, error writing block %lu\n", bh->b_blocknr) ;
	  }
	}
	bh = bh->b_this_page ;
      } while (bh != head) ;
    }
    if (!pl->do_not_lock) {
      UnlockPage(pl->page) ;
    }

    /* we've waited on the I/O, we can remove the page from the
    ** list, and free our pointer struct to it.
    */
    if (pl->prev) {
      pl->prev->next = pl->next ;
    }
    if (pl->next) {
      pl->next->prev = pl->prev ;
    }
    put_page(pl->page) ;
    pl_tmp = pl ;
    pl = pl->next ;
    reiserfs_kfree(pl_tmp, sizeof(struct reiserfs_page_list), p_s_sb) ;
  }
  SB_JOURNAL(p_s_sb)->j_flush_pages = NULL ;
}

/*
** called when a indirect item is converted back into a tail.
**
** The reiserfs part of the inode stores enough information to find
** our page_list struct in the flush list.  We remove it from the list
** and free the struct.
**
** Note, it is possible for this to happen:
**
** reiserfs_add_page_to_flush_list(inode)
** transaction ends, list is flushed
** reiserfs_remove_page_from_flush_list(inode)
**
** This would be bad because the page_list pointer in the inode is not
** updated when the list is flushed, so we can't know if the pointer is
** valid.  So, in the inode, we also store the transaction id when the
** page was added.  If we are trying to remove something from an old 
** transaction, we just clear out the pointer in the inode and return.
**
** Normal case is to use the reiserfs_page_list pointer in the inode to 
** find and remove the page from the flush list.
*/
int reiserfs_remove_page_from_flush_list(struct reiserfs_transaction_handle *th,
                                         struct inode *inode) {
  struct reiserfs_page_list *pl ;

  /* was this conversion done in a previous transaction? If so, return */
  if (inode->u.reiserfs_i.i_conversion_trans_id < th->t_trans_id) {
    inode->u.reiserfs_i.i_converted_page = NULL ;
    inode->u.reiserfs_i.i_conversion_trans_id = 0  ;
    return 0 ;
  }

  /* remove the page_list struct from the list, release our hold on the
  ** page, and free the page_list struct
  */
  pl = inode->u.reiserfs_i.i_converted_page ;
  if (pl) {
    if (pl->next) {
      pl->next->prev = pl->prev ;
    }
    if (pl->prev) {
      pl->prev->next = pl->next ;
    }
    if (SB_JOURNAL(inode->i_sb)->j_flush_pages == pl) {
      SB_JOURNAL(inode->i_sb)->j_flush_pages = pl->next ;
    }
    put_page(pl->page) ;
    reiserfs_kfree(pl, sizeof(struct reiserfs_page_list), inode->i_sb) ;
    inode->u.reiserfs_i.i_converted_page = NULL ;
    inode->u.reiserfs_i.i_conversion_trans_id = 0 ;
  }
  return 0 ;
}

/*
** Called after a direct to indirect transaction.  The unformatted node
** must be flushed to disk before the transaction commits, otherwise, we
** risk losing the data from the direct item.  This adds the page
** containing the unformatted node to a list of pages that need flushing.
**
** it calls get_page(page), so the page won't disappear until we've
** flushed or removed it from our list.
**
** pointers to the reiserfs_page_list struct are stored in the inode, 
** so this page can be quickly removed from the list after the tail is
** converted back into a direct item.
**
** If we fail to find the memory for the reiserfs_page_list struct, we
** just sync the page now.  Not good, but safe.
**
** since this must be called with the page locked, we always set
** the do_not_lock field in the page_list struct we allocate
**
*/
int reiserfs_add_page_to_flush_list(struct reiserfs_transaction_handle *th, 
                                    struct inode *inode,
				    struct buffer_head *bh) {
  struct reiserfs_page_list *new_pl ;

/* debugging use ONLY.  Do not define this on data you care about. */
#ifdef REISERFS_NO_FLUSH_AFTER_CONVERT
  return 0 ;
#endif

  get_page(bh->b_page) ;
  new_pl = reiserfs_kmalloc(sizeof(struct reiserfs_page_list), GFP_NOFS,
                            inode->i_sb) ;
  if (!new_pl) {
    put_page(bh->b_page) ;
    reiserfs_warning("journal-2480: forced to flush page, out of memory\n") ;
    ll_rw_block(WRITE, 1, &bh) ;
    wait_on_buffer(bh) ;
    if (!buffer_uptodate(bh)) {
      reiserfs_panic(inode->i_sb, "journal-2484: error writing buffer %lu to disk\n", bh->b_blocknr) ;
    }
    inode->u.reiserfs_i.i_converted_page = NULL ;
    return 0 ;
  }

  new_pl->page = bh->b_page ;
  new_pl->do_not_lock = 1 ;
  new_pl->blocknr = bh->b_blocknr ;
  new_pl->next = SB_JOURNAL(inode->i_sb)->j_flush_pages; 
  if (new_pl->next) {
    new_pl->next->prev = new_pl ;
  }
  new_pl->prev = NULL ;
  SB_JOURNAL(inode->i_sb)->j_flush_pages = new_pl ;
  
  /* if we have numbers from an old transaction, zero the converted
  ** page, it has already been flushed and freed
  */
  if (inode->u.reiserfs_i.i_conversion_trans_id &&
      inode->u.reiserfs_i.i_conversion_trans_id < th->t_trans_id) {
    inode->u.reiserfs_i.i_converted_page = NULL ;
  }
  if (inode->u.reiserfs_i.i_converted_page) {
    reiserfs_panic(inode->i_sb, "journal-2501: inode already had a converted page\n") ;
  }
  inode->u.reiserfs_i.i_converted_page = new_pl ;
  inode->u.reiserfs_i.i_conversion_trans_id = th->t_trans_id ;
  return 0 ;
}

/* 
** long and ugly.  If flush, will not return until all commit
** blocks and all real buffers in the trans are on disk.
** If no_async, won't return until all commit blocks are on disk.
**
** keep reading, there are comments as you go along
*/
static int do_journal_end(struct reiserfs_transaction_handle *th, struct super_block  * p_s_sb, unsigned long nblocks, 
		          int flags) {
  struct reiserfs_journal_cnode *cn, *next, *jl_cn; 
  struct reiserfs_journal_cnode *last_cn = NULL;
  struct reiserfs_journal_desc *desc ; 
  struct reiserfs_journal_commit *commit ; 
  struct buffer_head *c_bh ; /* commit bh */
  struct buffer_head *d_bh ; /* desc bh */
  int cur_write_start = 0 ; /* start index of current log write */
  int cur_blocks_left = 0 ; /* number of journal blocks left to write */
  int old_start ;
  int i ;
  int jindex ;
  int orig_jindex ;
  int flush = flags & FLUSH_ALL ;
  int commit_now = flags & COMMIT_NOW ;
  int wait_on_commit = flags & WAIT ;
  struct reiserfs_super_block *rs ; 
  int windex = push_journal_writer("do_journal_end") ;

  PROC_INFO_INC( p_s_sb, journal.mark_dirty );
  if (reiserfs_dont_log(th->t_super)) {
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* we must make sure all end_io tasks are done before we start marking
  ** buffers as BH_JDirty_wait.  Otherwise, they could be released and
  ** cleaned before they properly get to disk
  **
  ** we need the semaphore because other filesystems or journal writers
  ** could be running the queue at the same time.  We have to make sure the
  ** queue is *empty*, not just being worked on.
  */
  down(&reiserfs_end_io_sem) ;
  run_task_queue(&reiserfs_end_io_tq) ;
  up(&reiserfs_end_io_sem) ;


  lock_journal(p_s_sb) ;
  if (SB_JOURNAL(p_s_sb)->j_next_full_flush) {
    flags |= FLUSH_ALL ;
    flush = 1 ;
  }
  if (SB_JOURNAL(p_s_sb)->j_next_async_flush) {
    flags |= COMMIT_NOW ;
    commit_now = 1 ;
  }

  /* check_journal_end locks the journal, and unlocks if it does not return 1 
  ** it tells us if we should continue with the journal_end, or just return
  */
  if (!check_journal_end(th, p_s_sb, nblocks, flags)) {
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* check_journal_end might set these, check again */
  if (SB_JOURNAL(p_s_sb)->j_next_full_flush) {
    flush = 1 ;
  }
  if (SB_JOURNAL(p_s_sb)->j_next_async_flush) {
    commit_now = 1 ;
  }
  /*
  ** j must wait means we have to flush the log blocks, and the real blocks for
  ** this transaction
  */
  if (SB_JOURNAL(p_s_sb)->j_must_wait > 0) {
    flush = 1 ;
  }

#ifdef REISERFS_PREALLOCATE
  reiserfs_discard_all_prealloc(th); /* it should not involve new blocks into
				      * the transaction */
#endif
  
  rs = SB_DISK_SUPER_BLOCK(p_s_sb) ;
  /* setup description block */
  d_bh = getblk(p_s_sb->s_dev, rs->s_journal_block + SB_JOURNAL(p_s_sb)->j_start, p_s_sb->s_blocksize) ; 
  mark_buffer_uptodate(d_bh, 1) ;
  desc = (struct reiserfs_journal_desc *)(d_bh)->b_data ;
  memset(desc, 0, sizeof(struct reiserfs_journal_desc)) ;
  memcpy(desc->j_magic, JOURNAL_DESC_MAGIC, 8) ;
  desc->j_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;

  /* setup commit block.  Don't write (keep it clean too) this one until after everyone else is written */
  c_bh = getblk(p_s_sb->s_dev,  rs->s_journal_block + 
  				        ((SB_JOURNAL(p_s_sb)->j_start + SB_JOURNAL(p_s_sb)->j_len + 1) % JOURNAL_BLOCK_COUNT), 
					 p_s_sb->s_blocksize) ;
  d_bh->b_end_io = reiserfs_journal_end_io ;
  c_bh->b_end_io = reiserfs_journal_end_io ; 
  commit = (struct reiserfs_journal_commit *)c_bh->b_data ;
  memset(commit, 0, sizeof(struct reiserfs_journal_commit)) ;
  commit->j_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;
  mark_buffer_uptodate(c_bh, 1) ;

  /* init this journal list */
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_older_commits_done), 0) ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_timestamp = SB_JOURNAL(p_s_sb)->j_trans_start_time ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_commit_bh = c_bh ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_start = SB_JOURNAL(p_s_sb)->j_start ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_len = SB_JOURNAL(p_s_sb)->j_len ;  
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_nonzerolen), SB_JOURNAL(p_s_sb)->j_len) ;
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_commit_left), SB_JOURNAL(p_s_sb)->j_len + 2);
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_realblock = NULL ;
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_commit_flushing), 1) ;
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_flushing), 1) ;

  /* which is faster, locking/unlocking at the start and end of the for
  ** or locking once per iteration around the insert_journal_hash?
  ** eitherway, we are write locking insert_journal_hash.  The ENTIRE FOR
  ** LOOP MUST not cause schedule to occur.
  */

  /* for each real block, add it to the journal list hash,
  ** copy into real block index array in the commit or desc block
  */
  for (i = 0, cn = SB_JOURNAL(p_s_sb)->j_first ; cn ; cn = cn->next, i++) {
    if (test_bit(BH_JDirty, &cn->bh->b_state) ) {
      jl_cn = get_cnode(p_s_sb) ;
      if (!jl_cn) {
        reiserfs_panic(p_s_sb, "journal-1676, get_cnode returned NULL\n") ;
      }
      if (i == 0) {
        SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_realblock = jl_cn ;
      }
      jl_cn->prev = last_cn ;
      jl_cn->next = NULL ;
      if (last_cn) {
        last_cn->next = jl_cn ;
      }
      last_cn = jl_cn ;
      if (cn->bh->b_blocknr >= rs->s_journal_block &&
          cn->bh->b_blocknr < (rs->s_journal_block + JOURNAL_BLOCK_COUNT)) {
        reiserfs_panic(p_s_sb, "journal-2332: Trying to log block %lu, which is a log block\n", cn->bh->b_blocknr) ;
      }
      jl_cn->blocknr = cn->bh->b_blocknr ; 
      jl_cn->state = 0 ;
      jl_cn->dev = cn->bh->b_dev ; 
      jl_cn->bh = cn->bh ;
  if (cn->bh->b_end_io != reiserfs_end_buffer_io_sync) {
printk("warning do_journal_end has buffers without our end_io\n") ;
  }
      jl_cn->jlist = SB_JOURNAL_LIST(p_s_sb) + SB_JOURNAL_LIST_INDEX(p_s_sb) ;
      insert_journal_hash(SB_JOURNAL(p_s_sb)->j_list_hash_table, jl_cn) ; 
      if (i < JOURNAL_TRANS_HALF) {
	desc->j_realblock[i] = cn->bh->b_blocknr ;
      } else {
	commit->j_realblock[i - JOURNAL_TRANS_HALF] = cn->bh->b_blocknr ;
      }
    } else {
      i-- ;
    }
  }

  desc->j_len = SB_JOURNAL(p_s_sb)->j_len  ;
  desc->j_mount_id = SB_JOURNAL(p_s_sb)->j_mount_id ;
  desc->j_trans_id = SB_JOURNAL(p_s_sb)->j_trans_id ;
  commit->j_len = SB_JOURNAL(p_s_sb)->j_len  ;

  /* special check in case all buffers in the journal were marked for not logging */
  if (desc->j_len == 0) {
    brelse(d_bh) ;
    brelse(c_bh) ;
    unlock_journal(p_s_sb) ;
printk("journal-2020: do_journal_end: BAD desc->j_len is ZERO\n") ;
    atomic_set(&(SB_JOURNAL(p_s_sb)->j_jlock), 0) ;
    wake_up(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
    pop_journal_writer(windex) ;
    return 0 ;
  }

  /* first data block is j_start + 1, so add one to cur_write_start wherever you use it */
  cur_write_start = SB_JOURNAL(p_s_sb)->j_start ;
  cur_blocks_left = SB_JOURNAL(p_s_sb)->j_len  ;
  cn = SB_JOURNAL(p_s_sb)->j_first ;
  jindex = 1 ; /* start at one so we don't get the desc again */
  while(cur_blocks_left > 0) {
    /* copy all the real blocks into log area.  dirty log blocks */
    if (test_bit(BH_JDirty, &cn->bh->b_state)) {
      struct buffer_head *tmp_bh ;
      tmp_bh = getblk(p_s_sb->s_dev, rs->s_journal_block + 
		     ((cur_write_start + jindex) % JOURNAL_BLOCK_COUNT), 
				       p_s_sb->s_blocksize) ;
      tmp_bh->b_end_io = reiserfs_journal_end_io ;
      mark_buffer_uptodate(tmp_bh, 1) ;
      memcpy(tmp_bh->b_data, cn->bh->b_data, cn->bh->b_size) ;  
      jindex++ ;
    } else {
      /* JDirty cleared sometime during transaction.  don't log this one */
      printk("journal-2048: do_journal_end: BAD, buffer in journal hash, but not JDirty!\n") ;
    }
    cn = cn->next ;
    cur_blocks_left-- ;
  }

  /* we are done  with both the c_bh and d_bh, but
  ** c_bh must be written after all other commit blocks,
  ** so we dirty/relse c_bh in journal_end_io, with commit_left <= 1.
  */

  /* now loop through and mark all buffers from this transaction as JDirty_wait
  ** clear the JDirty bit, clear BH_JNew too.  
  ** if they weren't JDirty, they weren't logged, just relse them and move on
  */
  cn = SB_JOURNAL(p_s_sb)->j_first ; 
  while(cn) {
    clear_bit(BH_JNew, &(cn->bh->b_state)) ;
    if (test_bit(BH_JDirty, &(cn->bh->b_state))) {
      set_bit(BH_JDirty_wait, &(cn->bh->b_state)) ; 
      clear_bit(BH_JDirty, &(cn->bh->b_state)) ;
    } else {
      brelse(cn->bh) ;
    }
    next = cn->next ;
    free_cnode(p_s_sb, cn) ;
    cn = next ;
  }

  /* unlock the journal list for committing and flushing */
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_commit_flushing), 0) ;
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_flushing), 0) ;

  orig_jindex = SB_JOURNAL_LIST_INDEX(p_s_sb) ;
  jindex = (SB_JOURNAL_LIST_INDEX(p_s_sb) + 1) % JOURNAL_LIST_COUNT ; 
  SB_JOURNAL_LIST_INDEX(p_s_sb) = jindex ;

  /* write any buffers that must hit disk before this commit is done */
  fsync_inode_buffers(&(SB_JOURNAL(p_s_sb)->j_dummy_inode)) ;

  /* honor the flush and async wishes from the caller */
  if (flush) {
  
    flush_commit_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + orig_jindex, 1) ;
    flush_journal_list(p_s_sb,  SB_JOURNAL_LIST(p_s_sb) + orig_jindex , 0, 1) ;  /* flush all */
  } else if (commit_now) {
    if (wait_on_commit) {
      flush_commit_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + orig_jindex, 1) ;
    } else {
      commit_flush_async(p_s_sb, orig_jindex) ; 
    }
  }

  /* reset journal values for the next transaction */
  old_start = SB_JOURNAL(p_s_sb)->j_start ;
  SB_JOURNAL(p_s_sb)->j_start = (SB_JOURNAL(p_s_sb)->j_start + SB_JOURNAL(p_s_sb)->j_len + 2) % JOURNAL_BLOCK_COUNT;
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_wcount), 0) ;
  SB_JOURNAL(p_s_sb)->j_bcount = 0 ;
  SB_JOURNAL(p_s_sb)->j_last = NULL ;
  SB_JOURNAL(p_s_sb)->j_first = NULL ;
  SB_JOURNAL(p_s_sb)->j_len = 0 ;
  SB_JOURNAL(p_s_sb)->j_trans_start_time = 0 ;
  SB_JOURNAL(p_s_sb)->j_trans_id++ ;
  SB_JOURNAL(p_s_sb)->j_must_wait = 0 ;
  SB_JOURNAL(p_s_sb)->j_len_alloc = 0 ;
  SB_JOURNAL(p_s_sb)->j_next_full_flush = 0 ;
  SB_JOURNAL(p_s_sb)->j_next_async_flush = 0 ;
  init_journal_hash(p_s_sb) ; 

  /* if the next transaction has any chance of wrapping, flush 
  ** transactions that might get overwritten.  If any journal lists are very 
  ** old flush them as well.  Since data will get to disk every 30 seconds or
  ** so, any list that has unflushed members after 2 minutes was a victim to
  ** memory shortages during the end_io handler.  Clean things up for them
  **
  */
  for (i =0 ; i < JOURNAL_LIST_COUNT ; i++) {
    jindex = i ;
    if (SB_JOURNAL_LIST(p_s_sb)[jindex].j_len > 0 && SB_JOURNAL(p_s_sb)->j_start <= SB_JOURNAL_LIST(p_s_sb)[jindex].j_start) {
      if ((SB_JOURNAL(p_s_sb)->j_start + JOURNAL_TRANS_MAX + 1) >= SB_JOURNAL_LIST(p_s_sb)[jindex].j_start) {
	flush_journal_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + jindex, 0, 1) ; /* do flush all */
      }
    } else if (SB_JOURNAL_LIST(p_s_sb)[jindex].j_len > 0 && 
              (SB_JOURNAL(p_s_sb)->j_start + JOURNAL_TRANS_MAX + 1) > JOURNAL_BLOCK_COUNT) {
      if (((SB_JOURNAL(p_s_sb)->j_start + JOURNAL_TRANS_MAX + 1) % JOURNAL_BLOCK_COUNT) >= 
            SB_JOURNAL_LIST(p_s_sb)[jindex].j_start) {
	flush_journal_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + jindex, 0,1 ) ; /* do flush all */
      }
    } else if (SB_JOURNAL_LIST(p_s_sb)[jindex].j_len > 0 && 
              SB_JOURNAL_LIST(p_s_sb)[jindex].j_timestamp < 
	      (CURRENT_TIME - (JOURNAL_MAX_TRANS_AGE * 4))) {
	flush_journal_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + jindex, 0,1 ) ; 
    }
  }

  /* if the next journal_list is still in use, flush it */
  if (SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_len != 0) {
    flush_journal_list(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + SB_JOURNAL_LIST_INDEX(p_s_sb), 0, 1) ; /* do flush all */
  }

  /* we don't want anyone flushing the new transaction's list */
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_commit_flushing), 1) ;
  atomic_set(&(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_flushing), 1) ;
  SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_list_bitmap = get_list_bitmap(p_s_sb, SB_JOURNAL_LIST(p_s_sb) + 
											 SB_JOURNAL_LIST_INDEX(p_s_sb)) ;

  if (!(SB_JOURNAL_LIST(p_s_sb)[SB_JOURNAL_LIST_INDEX(p_s_sb)].j_list_bitmap)) {
    reiserfs_panic(p_s_sb, "journal-1996: do_journal_end, could not get a list bitmap\n") ;
  }
  unlock_journal(p_s_sb) ;
  atomic_set(&(SB_JOURNAL(p_s_sb)->j_jlock), 0) ;
  /* wake up any body waiting to join. */
  wake_up(&(SB_JOURNAL(p_s_sb)->j_join_wait)) ;
  pop_journal_writer(windex) ;
  return 0 ;
}




// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SQL_CONNECTION_H_
#define SQL_CONNECTION_H_

#include <map>
#include <set>
#include <string>

#include "build/build_config.h"
#if defined(__LB_SHELL__) || defined(STARBOARD)
#define SQL_CONNECTION_EXTRA_LOCKING
#endif

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#if defined(SQL_CONNECTION_EXTRA_LOCKING)
#include "base/synchronization/lock.h"
#endif
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "sql/sql_export.h"

namespace base{
  class FilePath;
}
struct sqlite3;
struct sqlite3_stmt;

namespace sql {

class Statement;

// Uniquely identifies a statement. There are two modes of operation:
//
// - In the most common mode, you will use the source file and line number to
//   identify your statement. This is a convienient way to get uniqueness for
//   a statement that is only used in one place. Use the SQL_FROM_HERE macro
//   to generate a StatementID.
//
// - In the "custom" mode you may use the statement from different places or
//   need to manage it yourself for whatever reason. In this case, you should
//   make up your own unique name and pass it to the StatementID. This name
//   must be a static string, since this object only deals with pointers and
//   assumes the underlying string doesn't change or get deleted.
//
// This object is copyable and assignable using the compiler-generated
// operator= and copy constructor.
class StatementID {
 public:
  // Creates a uniquely named statement with the given file ane line number.
  // Normally you will use SQL_FROM_HERE instead of calling yourself.
  StatementID(const char* file, int line)
      : number_(line),
        str_(file) {
  }

  // Creates a uniquely named statement with the given user-defined name.
  explicit StatementID(const char* unique_name)
      : number_(-1),
        str_(unique_name) {
  }

  // This constructor is unimplemented and will generate a linker error if
  // called. It is intended to try to catch people dynamically generating
  // a statement name that will be deallocated and will cause a crash later.
  // All strings must be static and unchanging!
  explicit StatementID(const std::string& dont_ever_do_this);

  // We need this to insert into our map.
  bool operator<(const StatementID& other) const;

 private:
  int number_;
  const char* str_;
};

#define SQL_FROM_HERE sql::StatementID(__FILE__, __LINE__)

class Connection;

// ErrorDelegate defines the interface to implement error handling and recovery
// for sqlite operations. This allows the rest of the classes to return true or
// false while the actual error code and causing statement are delivered using
// the OnError() callback.
// The tipical usage is to centralize the code designed to handle database
// corruption, low-level IO errors or locking violations.
class SQL_EXPORT ErrorDelegate {
 public:
  virtual ~ErrorDelegate();

  // |error| is an sqlite result code as seen in sqlite3.h. |connection| is the
  // db connection where the error happened and |stmt| is our best guess at the
  // statement that triggered the error. Do not store these pointers.
  //
  // |stmt| MAY BE NULL if there is no statement causing the problem (i.e. on
  // initialization).
  //
  // If the error condition has been fixed and the original statement succesfuly
  // re-tried then returning SQLITE_OK is appropriate; otherwise it is
  // recommended that you return the original |error| or the appropriate error
  // code.
  virtual int OnError(int error, Connection* connection, Statement* stmt) = 0;
};

class SQL_EXPORT Connection {
 private:
  class StatementRef;  // Forward declaration, see real one below.

 public:
  // The database is opened by calling Open[InMemory](). Any uncommitted
  // transactions will be rolled back when this object is deleted.
  Connection();
  ~Connection();

  // Pre-init configuration ----------------------------------------------------

  // Sets the page size that will be used when creating a new database. This
  // must be called before Init(), and will only have an effect on new
  // databases.
  //
  // From sqlite.org: "The page size must be a power of two greater than or
  // equal to 512 and less than or equal to SQLITE_MAX_PAGE_SIZE. The maximum
  // value for SQLITE_MAX_PAGE_SIZE is 32768."
  void set_page_size(int page_size) { page_size_ = page_size; }

  // Sets the number of pages that will be cached in memory by sqlite. The
  // total cache size in bytes will be page_size * cache_size. This must be
  // called before Open() to have an effect.
  void set_cache_size(int cache_size) { cache_size_ = cache_size; }

  // Call to put the database in exclusive locking mode. There is no "back to
  // normal" flag because of some additional requirements sqlite puts on this
  // transaition (requires another access to the DB) and because we don't
  // actually need it.
  //
  // Exclusive mode means that the database is not unlocked at the end of each
  // transaction, which means there may be less time spent initializing the
  // next transaction because it doesn't have to re-aquire locks.
  //
  // This must be called before Open() to have an effect.
  void set_exclusive_locking() { exclusive_locking_ = true; }

  // Sets the object that will handle errors. Recomended that it should be set
  // before calling Open(). If not set, the default is to ignore errors on
  // release and assert on debug builds.
  // Takes ownership of |delegate|.
  void set_error_delegate(ErrorDelegate* delegate) {
    error_delegate_.reset(delegate);
  }

  // Initialization ------------------------------------------------------------

  // Initializes the SQL connection for the given file, returning true if the
  // file could be opened. You can call this or OpenInMemory.
  bool Open(const base::FilePath& path) WARN_UNUSED_RESULT;

  // Initializes the SQL connection for a temporary in-memory database. There
  // will be no associated file on disk, and the initial database will be
  // empty. You can call this or Open.
  bool OpenInMemory() WARN_UNUSED_RESULT;

  // Returns trie if the database has been successfully opened.
  bool is_open() const { return !!db_; }

  // Closes the database. This is automatically performed on destruction for
  // you, but this allows you to close the database early. You must not call
  // any other functions after closing it. It is permissable to call Close on
  // an uninitialized or already-closed database.
  void Close();

  // Pre-loads the first <cache-size> pages into the cache from the file.
  // If you expect to soon use a substantial portion of the database, this
  // is much more efficient than allowing the pages to be populated organically
  // since there is no per-page hard drive seeking. If the file is larger than
  // the cache, the last part that doesn't fit in the cache will be brought in
  // organically.
  //
  // This function assumes your class is using a meta table on the current
  // database, as it openes a transaction on the meta table to force the
  // database to be initialized. You should feel free to initialize the meta
  // table after calling preload since the meta table will already be in the
  // database if it exists, and if it doesn't exist, the database won't
  // generally exist either.
  void Preload();

  // Raze the database to the ground.  This approximates creating a
  // fresh database from scratch, within the constraints of SQLite's
  // locking protocol (locks and open handles can make doing this with
  // filesystem operations problematic).  Returns true if the database
  // was razed.
  //
  // false is returned if the database is locked by some other
  // process.  RazeWithTimeout() may be used if appropriate.
  //
  // NOTE(shess): Raze() will DCHECK in the following situations:
  // - database is not open.
  // - the connection has a transaction open.
  // - a SQLite issue occurs which is structural in nature (like the
  //   statements used are broken).
  // Since Raze() is expected to be called in unexpected situations,
  // these all return false, since it is unlikely that the caller
  // could fix them.
  //
  // The database's page size is taken from |page_size_|.  The
  // existing database's |auto_vacuum| setting is lost (the
  // possibility of corruption makes it unreliable to pull it from the
  // existing database).  To re-enable on the empty database requires
  // running "PRAGMA auto_vacuum = 1;" then "VACUUM".
  //
  // NOTE(shess): For Android, SQLITE_DEFAULT_AUTOVACUUM is set to 1,
  // so Raze() sets auto_vacuum to 1.
  //
  // TODO(shess): Raze() needs a connection so cannot clear SQLITE_NOTADB.
  // TODO(shess): Bake auto_vacuum into Connection's API so it can
  // just pick up the default.
  bool Raze();
  bool RazeWithTimout(base::TimeDelta timeout);

  // Clone another database into this one.
  bool CloneFrom(Connection *other);

  // Transactions --------------------------------------------------------------

  // Transaction management. We maintain a virtual transaction stack to emulate
  // nested transactions since sqlite can't do nested transactions. The
  // limitation is you can't roll back a sub transaction: if any transaction
  // fails, all transactions open will also be rolled back. Any nested
  // transactions after one has rolled back will return fail for Begin(). If
  // Begin() fails, you must not call Commit or Rollback().
  //
  // Normally you should use sql::Transaction to manage a transaction, which
  // will scope it to a C++ context.
  bool BeginTransaction();
  void RollbackTransaction();
  bool CommitTransaction();

  // Returns the current transaction nesting, which will be 0 if there are
  // no open transactions.
  int transaction_nesting() const { return transaction_nesting_; }

  // Statements ----------------------------------------------------------------

  // Executes the given SQL string, returning true on success. This is
  // normally used for simple, 1-off statements that don't take any bound
  // parameters and don't return any data (e.g. CREATE TABLE).
  //
  // This will DCHECK if the |sql| contains errors.
  //
  // Do not use ignore_result() to ignore all errors.  Use
  // ExecuteAndReturnErrorCode() and ignore only specific errors.
  bool Execute(const char* sql) WARN_UNUSED_RESULT;

  // Like Execute(), but returns the error code given by SQLite.
  int ExecuteAndReturnErrorCode(const char* sql) WARN_UNUSED_RESULT;

  // Returns true if we have a statement with the given identifier already
  // cached. This is normally not necessary to call, but can be useful if the
  // caller has to dynamically build up SQL to avoid doing so if it's already
  // cached.
  bool HasCachedStatement(const StatementID& id) const;

  // Returns a statement for the given SQL using the statement cache. It can
  // take a nontrivial amount of work to parse and compile a statement, so
  // keeping commonly-used ones around for future use is important for
  // performance.
  //
  // If the |sql| has an error, an invalid, inert StatementRef is returned (and
  // the code will crash in debug). The caller must deal with this eventuality,
  // either by checking validity of the |sql| before calling, by correctly
  // handling the return of an inert statement, or both.
  //
  // The StatementID and the SQL must always correspond to one-another. The
  // ID is the lookup into the cache, so crazy things will happen if you use
  // different SQL with the same ID.
  //
  // You will normally use the SQL_FROM_HERE macro to generate a statement
  // ID associated with the current line of code. This gives uniqueness without
  // you having to manage unique names. See StatementID above for more.
  //
  // Example:
  //   sql::Statement stmt(connection_.GetCachedStatement(
  //       SQL_FROM_HERE, "SELECT * FROM foo"));
  //   if (!stmt)
  //     return false;  // Error creating statement.
  scoped_refptr<StatementRef> GetCachedStatement(const StatementID& id,
                                                 const char* sql);

  // Used to check a |sql| statement for syntactic validity. If the statement is
  // valid SQL, returns true.
  bool IsSQLValid(const char* sql);

  // Returns a non-cached statement for the given SQL. Use this for SQL that
  // is only executed once or only rarely (there is overhead associated with
  // keeping a statement cached).
  //
  // See GetCachedStatement above for examples and error information.
  scoped_refptr<StatementRef> GetUniqueStatement(const char* sql,
                                                 bool must_succeed = true);

  // Info querying -------------------------------------------------------------

  // Returns true if the given table exists.
  bool DoesTableExist(const char* table_name) const;

  // Returns true if the given index exists.
  bool DoesIndexExist(const char* index_name) const;

  // Returns true if a column with the given name exists in the given table.
  bool DoesColumnExist(const char* table_name, const char* column_name) const;

  // Returns sqlite's internal ID for the last inserted row. Valid only
  // immediately after an insert.
  int64 GetLastInsertRowId() const;

  // Returns sqlite's count of the number of rows modified by the last
  // statement executed. Will be 0 if no statement has executed or the database
  // is closed.
  int GetLastChangeCount() const;

  // Errors --------------------------------------------------------------------

  // Returns the error code associated with the last sqlite operation.
  int GetErrorCode() const;

  // Returns the errno associated with GetErrorCode().  See
  // SQLITE_LAST_ERRNO in SQLite documentation.
  int GetLastErrno() const;

  // Returns a pointer to a statically allocated string associated with the
  // last sqlite operation.
  const char* GetErrorMessage() const;

 private:
  // Statement accesses StatementRef which we don't want to expose to everybody
  // (they should go through Statement).
  friend class Statement;

  // Internal initialize function used by both Init and InitInMemory. The file
  // name is always 8 bits since we want to use the 8-bit version of
  // sqlite3_open. The string can also be sqlite's special ":memory:" string.
  bool OpenInternal(const std::string& file_name);

  // Check whether the current thread is allowed to make IO calls, but only
  // if database wasn't open in memory. Function is inlined to be a no-op in
  // official build.
  void AssertIOAllowed() {
#ifdef STARBOARD
    // Note: SetIOAllowed is deprecated, consider deprecating related functions
    // and use ScopedDisallowBlocking when we upgrade sql.
    if (!in_memory_)
      base::AssertBlockingAllowed();
#else
    if (!in_memory_)
      base::ThreadRestrictions::AssertIOAllowed();
#endif
  }

  // Internal helper for DoesTableExist and DoesIndexExist.
  bool DoesTableOrIndexExist(const char* name, const char* type) const;

  // A StatementRef is a refcounted wrapper around a sqlite statement pointer.
  // Refcounting allows us to give these statements out to sql::Statement
  // objects while also optionally maintaining a cache of compiled statements
  // by just keeping a refptr to these objects.
  //
  // A statement ref can be valid, in which case it can be used, or invalid to
  // indicate that the statement hasn't been created yet, has an error, or has
  // been destroyed.
  //
  // The Connection may revoke a StatementRef in some error cases, so callers
  // should always check validity before using.
  class SQL_EXPORT StatementRef : public base::RefCounted<StatementRef> {
   public:
    // Default constructor initializes to an invalid statement.
    StatementRef();
    explicit StatementRef(sqlite3_stmt* stmt);
    StatementRef(Connection* connection, sqlite3_stmt* stmt);

    // When true, the statement can be used.
    bool is_valid() const { return !!stmt_; }

    // If we've not been linked to a connection, this will be NULL. Guaranteed
    // non-NULL when is_valid().
    Connection* connection() const { return connection_; }

    // Returns the sqlite statement if any. If the statement is not active,
    // this will return NULL.
    sqlite3_stmt* stmt() const { return stmt_; }

    // Destroys the compiled statement and marks it NULL. The statement will
    // no longer be active.
    void Close();

    // Check whether the current thread is allowed to make IO calls, but only
    // if database wasn't open in memory.
    void AssertIOAllowed() { if (connection_) connection_->AssertIOAllowed(); }

   private:
    friend class base::RefCounted<StatementRef>;

    ~StatementRef();

    Connection* connection_;
    sqlite3_stmt* stmt_;

    DISALLOW_COPY_AND_ASSIGN(StatementRef);
  };
  friend class StatementRef;

  // Shared code between CloneFrom and Raze.
  bool CloneInternal(Connection *other, int *pages);

  // Executes a rollback statement, ignoring all transaction state. Used
  // internally in the transaction management code.
  void DoRollback();

  // Called by a StatementRef when it's being created or destroyed. See
  // open_statements_ below.
  void StatementRefCreated(StatementRef* ref);
  void StatementRefDeleted(StatementRef* ref);

  // Frees all cached statements from statement_cache_.
  void ClearCache();

  // Called by Statement objects when an sqlite function returns an error.
  // The return value is the error code reflected back to client code.
  int OnSqliteError(int err, Statement* stmt);

  // Like |Execute()|, but retries if the database is locked.
  bool ExecuteWithTimeout(const char* sql, base::TimeDelta ms_timeout)
      WARN_UNUSED_RESULT;

  // Internal helper for const functions.  Like GetUniqueStatement(),
  // except the statement is not entered into open_statements_,
  // allowing this function to be const.  Open statements can block
  // closing the database, so only use in cases where the last ref is
  // released before close could be called (which should always be the
  // case for const functions).
  scoped_refptr<StatementRef> GetUntrackedStatement(const char* sql) const;

  // The actual sqlite database. Will be NULL before Init has been called or if
  // Init resulted in an error.
  sqlite3* db_;

  // Parameters we'll configure in sqlite before doing anything else. Zero means
  // use the default value.
  int page_size_;
  int cache_size_;
  bool exclusive_locking_;

  // All cached statements. Keeping a reference to these statements means that
  // they'll remain active.
  typedef std::map<StatementID, scoped_refptr<StatementRef> >
      CachedStatementMap;
  CachedStatementMap statement_cache_;

  // A list of all StatementRefs we've given out. Each ref must register with
  // us when it's created or destroyed. This allows us to potentially close
  // any open statements when we encounter an error.
  typedef std::set<StatementRef*> StatementRefSet;
  StatementRefSet open_statements_;

  // Number of currently-nested transactions.
  int transaction_nesting_;

  // True if any of the currently nested transactions have been rolled back.
  // When we get to the outermost transaction, this will determine if we do
  // a rollback instead of a commit.
  bool needs_rollback_;

  // True if database is open with OpenInMemory(), False if database is open
  // with Open().
  bool in_memory_;

  // This object handles errors resulting from all forms of executing sqlite
  // commands or statements. It can be null which means default handling.
  std::unique_ptr<ErrorDelegate> error_delegate_;


#if defined(SQL_CONNECTION_EXTRA_LOCKING)
  // In lb shell this object is used in multiple threads, and STL objects are
  // not thread safe by themselves. These locks are used to protect
  // statement_cache_ and open_statements_.
  mutable base::Lock statement_cache_lock_;
  base::Lock open_statements_lock_;
#endif

  DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace sql

#endif  // SQL_CONNECTION_H_

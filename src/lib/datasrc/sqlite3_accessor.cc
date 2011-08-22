// Copyright (C) 2011  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <sqlite3.h>

#include <datasrc/sqlite3_accessor.h>
#include <datasrc/logger.h>
#include <datasrc/data_source.h>
#include <util/filename.h>

#include <boost/lexical_cast.hpp>

#define SQLITE_SCHEMA_VERSION 1

namespace isc {
namespace datasrc {

struct SQLite3Parameters {
    SQLite3Parameters() :
        db_(NULL), version_(-1),
        q_zone_(NULL), q_any_(NULL)
        /*q_record_(NULL), q_addrs_(NULL), q_referral_(NULL),
        q_count_(NULL), q_previous_(NULL), q_nsec3_(NULL),
        q_prevnsec3_(NULL) */
    {}
    sqlite3* db_;
    int version_;
    sqlite3_stmt* q_zone_;
    sqlite3_stmt* q_any_;
    /*
    TODO: Yet unneeded statements
    sqlite3_stmt* q_record_;
    sqlite3_stmt* q_addrs_;
    sqlite3_stmt* q_referral_;
    sqlite3_stmt* q_count_;
    sqlite3_stmt* q_previous_;
    sqlite3_stmt* q_nsec3_;
    sqlite3_stmt* q_prevnsec3_;
    */
};

SQLite3Database::SQLite3Database(const std::string& filename,
                                     const isc::dns::RRClass& rrclass) :
    dbparameters_(new SQLite3Parameters),
    class_(rrclass.toText()),
    database_name_("sqlite3_" +
                   isc::util::Filename(filename).nameAndExtension())
{
    LOG_DEBUG(logger, DBG_TRACE_BASIC, DATASRC_SQLITE_NEWCONN);

    open(filename);
}

namespace {

// This is a helper class to initialize a Sqlite3 DB safely.  An object of
// this class encapsulates all temporary resources that are necessary for
// the initialization, and release them in the destructor.  Once everything
// is properly initialized, the move() method moves the allocated resources
// to the main object in an exception free manner.  This way, the main code
// for the initialization can be exception safe, and can provide the strong
// exception guarantee.
class Initializer {
public:
    ~Initializer() {
        if (params_.q_zone_ != NULL) {
            sqlite3_finalize(params_.q_zone_);
        }
        if (params_.q_any_ != NULL) {
            sqlite3_finalize(params_.q_any_);
        }
        /*
        if (params_.q_record_ != NULL) {
            sqlite3_finalize(params_.q_record_);
        }
        if (params_.q_addrs_ != NULL) {
            sqlite3_finalize(params_.q_addrs_);
        }
        if (params_.q_referral_ != NULL) {
            sqlite3_finalize(params_.q_referral_);
        }
        if (params_.q_count_ != NULL) {
            sqlite3_finalize(params_.q_count_);
        }
        if (params_.q_previous_ != NULL) {
            sqlite3_finalize(params_.q_previous_);
        }
        if (params_.q_nsec3_ != NULL) {
            sqlite3_finalize(params_.q_nsec3_);
        }
        if (params_.q_prevnsec3_ != NULL) {
            sqlite3_finalize(params_.q_prevnsec3_);
        }
        */
        if (params_.db_ != NULL) {
            sqlite3_close(params_.db_);
        }
    }
    void move(SQLite3Parameters* dst) {
        *dst = params_;
        params_ = SQLite3Parameters(); // clear everything
    }
    SQLite3Parameters params_;
};

const char* const SCHEMA_LIST[] = {
    "CREATE TABLE schema_version (version INTEGER NOT NULL)",
    "INSERT INTO schema_version VALUES (1)",
    "CREATE TABLE zones (id INTEGER PRIMARY KEY, "
    "name STRING NOT NULL COLLATE NOCASE, "
    "rdclass STRING NOT NULL COLLATE NOCASE DEFAULT 'IN', "
    "dnssec BOOLEAN NOT NULL DEFAULT 0)",
    "CREATE INDEX zones_byname ON zones (name)",
    "CREATE TABLE records (id INTEGER PRIMARY KEY, "
    "zone_id INTEGER NOT NULL, name STRING NOT NULL COLLATE NOCASE, "
    "rname STRING NOT NULL COLLATE NOCASE, ttl INTEGER NOT NULL, "
    "rdtype STRING NOT NULL COLLATE NOCASE, sigtype STRING COLLATE NOCASE, "
    "rdata STRING NOT NULL)",
    "CREATE INDEX records_byname ON records (name)",
    "CREATE INDEX records_byrname ON records (rname)",
    "CREATE TABLE nsec3 (id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
    "hash STRING NOT NULL COLLATE NOCASE, "
    "owner STRING NOT NULL COLLATE NOCASE, "
    "ttl INTEGER NOT NULL, rdtype STRING NOT NULL COLLATE NOCASE, "
    "rdata STRING NOT NULL)",
    "CREATE INDEX nsec3_byhash ON nsec3 (hash)",
    NULL
};

const char* const q_version_str = "SELECT version FROM schema_version";

const char* const q_zone_str = "SELECT id FROM zones WHERE name=?1 AND rdclass = ?2";

const char* const q_any_str = "SELECT rdtype, ttl, sigtype, rdata, name "
    "FROM records WHERE zone_id=?1 AND name=?2";

const char* const q_iterate_str = "SELECT rdtype, ttl, sigtype, rdata, name FROM records "
                                  "WHERE zone_id = ?1 "
                                  "ORDER BY name, rdtype";

/* TODO: Prune the statements, not everything will be needed maybe?
const char* const q_record_str = "SELECT rdtype, ttl, sigtype, rdata "
    "FROM records WHERE zone_id=?1 AND name=?2 AND "
    "((rdtype=?3 OR sigtype=?3) OR "
    "(rdtype='CNAME' OR sigtype='CNAME') OR "
    "(rdtype='NS' OR sigtype='NS'))";

const char* const q_addrs_str = "SELECT rdtype, ttl, sigtype, rdata "
    "FROM records WHERE zone_id=?1 AND name=?2 AND "
    "(rdtype='A' OR sigtype='A' OR rdtype='AAAA' OR sigtype='AAAA')";

const char* const q_referral_str = "SELECT rdtype, ttl, sigtype, rdata FROM "
    "records WHERE zone_id=?1 AND name=?2 AND"
    "(rdtype='NS' OR sigtype='NS' OR rdtype='DS' OR sigtype='DS' OR "
    "rdtype='DNAME' OR sigtype='DNAME')";

const char* const q_count_str = "SELECT COUNT(*) FROM records "
    "WHERE zone_id=?1 AND rname LIKE (?2 || '%');";

const char* const q_previous_str = "SELECT name FROM records "
    "WHERE zone_id=?1 AND rdtype = 'NSEC' AND "
    "rname < $2 ORDER BY rname DESC LIMIT 1";

const char* const q_nsec3_str = "SELECT rdtype, ttl, rdata FROM nsec3 "
    "WHERE zone_id = ?1 AND hash = $2";

const char* const q_prevnsec3_str = "SELECT hash FROM nsec3 "
    "WHERE zone_id = ?1 AND hash <= $2 ORDER BY hash DESC LIMIT 1";
    */

sqlite3_stmt*
prepare(sqlite3* const db, const char* const statement) {
    sqlite3_stmt* prepared = NULL;
    if (sqlite3_prepare_v2(db, statement, -1, &prepared, NULL) != SQLITE_OK) {
        isc_throw(SQLite3Error, "Could not prepare SQLite statement: " <<
                  statement);
    }
    return (prepared);
}

// small function to sleep for 0.1 seconds, needed when waiting for
// exclusive database locks (which should only occur on startup, and only
// when the database has not been created yet)
void do_sleep() {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 100000000;
    nanosleep(&req, NULL);
}

// returns the schema version if the schema version table exists
// returns -1 if it does not
int check_schema_version(sqlite3* db) {
    sqlite3_stmt* prepared = NULL;
    // At this point in time, the database might be exclusively locked, in
    // which case even prepare() will return BUSY, so we may need to try a
    // few times
    for (size_t i = 0; i < 50; ++i) {
        int rc = sqlite3_prepare_v2(db, q_version_str, -1, &prepared, NULL);
        if (rc == SQLITE_ERROR) {
            // this is the error that is returned when the table does not
            // exist
            return (-1);
        } else if (rc == SQLITE_OK) {
            break;
        } else if (rc != SQLITE_BUSY || i == 50) {
            isc_throw(SQLite3Error, "Unable to prepare version query: "
                        << rc << " " << sqlite3_errmsg(db));
        }
        do_sleep();
    }
    if (sqlite3_step(prepared) != SQLITE_ROW) {
        isc_throw(SQLite3Error,
                    "Unable to query version: " << sqlite3_errmsg(db));
    }
    int version = sqlite3_column_int(prepared, 0);
    sqlite3_finalize(prepared);
    return (version);
}

// return db version
int create_database(sqlite3* db) {
    // try to get an exclusive lock. Once that is obtained, do the version
    // check *again*, just in case this process was racing another
    //
    // try for 5 secs (50*0.1)
    int rc;
    logger.info(DATASRC_SQLITE_SETUP);
    for (size_t i = 0; i < 50; ++i) {
        rc = sqlite3_exec(db, "BEGIN EXCLUSIVE TRANSACTION", NULL, NULL,
                            NULL);
        if (rc == SQLITE_OK) {
            break;
        } else if (rc != SQLITE_BUSY || i == 50) {
            isc_throw(SQLite3Error, "Unable to acquire exclusive lock "
                        "for database creation: " << sqlite3_errmsg(db));
        }
        do_sleep();
    }
    int schema_version = check_schema_version(db);
    if (schema_version == -1) {
        for (int i = 0; SCHEMA_LIST[i] != NULL; ++i) {
            if (sqlite3_exec(db, SCHEMA_LIST[i], NULL, NULL, NULL) !=
                SQLITE_OK) {
                isc_throw(SQLite3Error,
                        "Failed to set up schema " << SCHEMA_LIST[i]);
            }
        }
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        return (SQLITE_SCHEMA_VERSION);
    } else {
        return (schema_version);
    }
}

void
checkAndSetupSchema(Initializer* initializer) {
    sqlite3* const db = initializer->params_.db_;

    int schema_version = check_schema_version(db);
    if (schema_version != SQLITE_SCHEMA_VERSION) {
        schema_version = create_database(db);
    }
    initializer->params_.version_ = schema_version;

    initializer->params_.q_zone_ = prepare(db, q_zone_str);
    initializer->params_.q_any_ = prepare(db, q_any_str);
    /* TODO: Yet unneeded statements
    initializer->params_.q_record_ = prepare(db, q_record_str);
    initializer->params_.q_addrs_ = prepare(db, q_addrs_str);
    initializer->params_.q_referral_ = prepare(db, q_referral_str);
    initializer->params_.q_count_ = prepare(db, q_count_str);
    initializer->params_.q_previous_ = prepare(db, q_previous_str);
    initializer->params_.q_nsec3_ = prepare(db, q_nsec3_str);
    initializer->params_.q_prevnsec3_ = prepare(db, q_prevnsec3_str);
    */
}

}

void
SQLite3Database::open(const std::string& name) {
    LOG_DEBUG(logger, DBG_TRACE_BASIC, DATASRC_SQLITE_CONNOPEN).arg(name);
    if (dbparameters_->db_ != NULL) {
        // There shouldn't be a way to trigger this anyway
        isc_throw(DataSourceError, "Duplicate SQLite open with " << name);
    }

    Initializer initializer;

    if (sqlite3_open(name.c_str(), &initializer.params_.db_) != 0) {
        isc_throw(SQLite3Error, "Cannot open SQLite database file: " << name);
    }

    checkAndSetupSchema(&initializer);
    initializer.move(dbparameters_);
}

SQLite3Database::~SQLite3Database() {
    LOG_DEBUG(logger, DBG_TRACE_BASIC, DATASRC_SQLITE_DROPCONN);
    if (dbparameters_->db_ != NULL) {
        close();
    }
    delete dbparameters_;
}

void
SQLite3Database::close(void) {
    LOG_DEBUG(logger, DBG_TRACE_BASIC, DATASRC_SQLITE_CONNCLOSE);
    if (dbparameters_->db_ == NULL) {
        isc_throw(DataSourceError,
                  "SQLite data source is being closed before open");
    }

    // XXX: sqlite3_finalize() could fail.  What should we do in that case?
    sqlite3_finalize(dbparameters_->q_zone_);
    dbparameters_->q_zone_ = NULL;

    sqlite3_finalize(dbparameters_->q_any_);
    dbparameters_->q_any_ = NULL;

    /* TODO: Once they are needed or not, uncomment or drop
    sqlite3_finalize(dbparameters->q_record_);
    dbparameters->q_record_ = NULL;

    sqlite3_finalize(dbparameters->q_addrs_);
    dbparameters->q_addrs_ = NULL;

    sqlite3_finalize(dbparameters->q_referral_);
    dbparameters->q_referral_ = NULL;

    sqlite3_finalize(dbparameters->q_count_);
    dbparameters->q_count_ = NULL;

    sqlite3_finalize(dbparameters->q_previous_);
    dbparameters->q_previous_ = NULL;

    sqlite3_finalize(dbparameters->q_prevnsec3_);
    dbparameters->q_prevnsec3_ = NULL;

    sqlite3_finalize(dbparameters->q_nsec3_);
    dbparameters->q_nsec3_ = NULL;
    */

    sqlite3_close(dbparameters_->db_);
    dbparameters_->db_ = NULL;
}

std::pair<bool, int>
SQLite3Database::getZone(const isc::dns::Name& name) const {
    int rc;

    // Take the statement (simple SELECT id FROM zones WHERE...)
    // and prepare it (bind the parameters to it)
    sqlite3_reset(dbparameters_->q_zone_);
    rc = sqlite3_bind_text(dbparameters_->q_zone_, 1, name.toText().c_str(),
                           -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        isc_throw(SQLite3Error, "Could not bind " << name <<
                  " to SQL statement (zone)");
    }
    rc = sqlite3_bind_text(dbparameters_->q_zone_, 2, class_.c_str(), -1,
                           SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        isc_throw(SQLite3Error, "Could not bind " << class_ <<
                  " to SQL statement (zone)");
    }

    // Get the data there and see if it found anything
    rc = sqlite3_step(dbparameters_->q_zone_);
    std::pair<bool, int> result;
    if (rc == SQLITE_ROW) {
        result = std::pair<bool, int>(true,
                                      sqlite3_column_int(dbparameters_->
                                                         q_zone_, 0));
        return (result);
    } else if (rc == SQLITE_DONE) {
        result = std::pair<bool, int>(false, 0);
        // Free resources
        sqlite3_reset(dbparameters_->q_zone_);
        return (result);
    }

    isc_throw(DataSourceError, "Unexpected failure in sqlite3_step: " <<
                               sqlite3_errmsg(dbparameters_->db_));
    // Compilers might not realize isc_throw always throws
    return (std::pair<bool, int>(false, 0));
}

namespace {
// This helper function converts from the unsigned char* type (used by
// sqlite3) to char* (wanted by std::string). Technically these types
// might not be directly convertable
// In case sqlite3_column_text() returns NULL, we just make it an
// empty string.
// The sqlite3parameters value is only used to check the error code if
// ucp == NULL
const char*
convertToPlainChar(const unsigned char* ucp,
                   SQLite3Parameters* dbparameters) {
    if (ucp == NULL) {
        // The field can really be NULL, in which case we return an
        // empty string, or sqlite may have run out of memory, in
        // which case we raise an error
        if (dbparameters != NULL &&
            sqlite3_errcode(dbparameters->db_) == SQLITE_NOMEM) {
            isc_throw(DataSourceError,
                      "Sqlite3 backend encountered a memory allocation "
                      "error in sqlite3_column_text()");
        } else {
            return ("");
        }
    }
    const void* p = ucp;
    return (static_cast<const char*>(p));
}
}

// TODO: Once we want to have iterator returned from searchForRecords, this
// class can be reused. It should be modified to take the sqlite3 statement
// instead of creating it in constructor, it doesn't have to care which one
// it is, just provide data from it.
class SQLite3Database::Context : public DatabaseAccessor::IteratorContext {
public:
    Context(const boost::shared_ptr<const SQLite3Database>& database, int id) :
        database_(database),
        statement(NULL)
    {
        // We create the statement now and then just keep getting data from it
        statement = prepare(database->dbparameters_->db_, q_iterate_str);
        if (sqlite3_bind_int(statement, 1, id) != SQLITE_OK) {
            isc_throw(SQLite3Error, "Could not bind " << id <<
                      " to SQL statement (iterate)");
        }
    }
    bool getNext(std::string data[], size_t size) {
        if (size != COLUMN_COUNT) {
            isc_throw(DataSourceError, "getNext received size of " << size <<
                      ", not " << COLUMN_COUNT);
        }
        // If there's another row, get it
        int rc(sqlite3_step(statement));
        if (rc == SQLITE_ROW) {
            for (size_t i(0); i < size; ++ i) {
                data[i] = convertToPlainChar(sqlite3_column_text(statement, i),
                                             database_->dbparameters_);
            }
            return (true);
        } else if (rc != SQLITE_DONE) {
            isc_throw(DataSourceError,
                      "Unexpected failure in sqlite3_step: " <<
                      sqlite3_errmsg(database_->dbparameters_->db_));
        }
        return (false);
    }
    virtual ~Context() {
        if (statement) {
            sqlite3_finalize(statement);
        }
    }

private:
    boost::shared_ptr<const SQLite3Database> database_;
    sqlite3_stmt *statement;
};

DatabaseAccessor::IteratorContextPtr
SQLite3Database::getAllRecords(const isc::dns::Name&, int id) const {
    return (IteratorContextPtr(new Context(shared_from_this(), id)));
}

void
SQLite3Database::searchForRecords(int zone_id, const std::string& name) {
    resetSearch();
    if (sqlite3_bind_int(dbparameters_->q_any_, 1, zone_id) != SQLITE_OK) {
        isc_throw(DataSourceError,
                  "Error in sqlite3_bind_int() for zone_id " <<
                  zone_id << ": " << sqlite3_errmsg(dbparameters_->db_));
    }
    // use transient since name is a ref and may disappear
    if (sqlite3_bind_text(dbparameters_->q_any_, 2, name.c_str(), -1,
                               SQLITE_TRANSIENT) != SQLITE_OK) {
        isc_throw(DataSourceError,
                  "Error in sqlite3_bind_text() for name " <<
                  name << ": " << sqlite3_errmsg(dbparameters_->db_));
    }
}

bool
SQLite3Database::getNextRecord(std::string columns[], size_t column_count) {
    if (column_count != COLUMN_COUNT) {
            isc_throw(DataSourceError,
                    "Datasource backend caller did not pass a column array "
                    "of size " << COLUMN_COUNT << " to getNextRecord()");
    }

    sqlite3_stmt* current_stmt = dbparameters_->q_any_;
    const int rc = sqlite3_step(current_stmt);

    if (rc == SQLITE_ROW) {
        for (int column = 0; column < column_count; ++column) {
            try {
                columns[column] = convertToPlainChar(sqlite3_column_text(
                                                     current_stmt, column),
                                                     dbparameters_);
            } catch (const std::bad_alloc&) {
                isc_throw(DataSourceError,
                        "bad_alloc in Sqlite3Connection::getNextRecord");
            }
        }
        return (true);
    } else if (rc == SQLITE_DONE) {
        // reached the end of matching rows
        resetSearch();
        return (false);
    }
    isc_throw(DataSourceError, "Unexpected failure in sqlite3_step: " <<
                               sqlite3_errmsg(dbparameters_->db_));
    // Compilers might not realize isc_throw always throws
    return (false);
}

void
SQLite3Database::resetSearch() {
    sqlite3_reset(dbparameters_->q_any_);
    sqlite3_clear_bindings(dbparameters_->q_any_);
}

}
}

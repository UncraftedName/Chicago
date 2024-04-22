#pragma once

#include <stdbool.h>

typedef enum ch_field_type {
    FIELD_VOID = 0,   // No type or value
    FIELD_FLOAT,      // Any floating point value
    FIELD_STRING,     // A string ID (return from ALLOC_STRING)
    FIELD_VECTOR,     // Any vector, QAngle, or AngularImpulse
    FIELD_QUATERNION, // A quaternion
    FIELD_INTEGER,    // Any integer or enum
    FIELD_BOOLEAN,    // boolean, implemented as an int, I may use this as a hint for compression
    FIELD_SHORT,      // 2 byte integer
    FIELD_CHARACTER,  // a byte
    FIELD_COLOR32,    // 8-bit per channel r,g,b,a (32bit color)
    FIELD_EMBEDDED, // an embedded object with a datadesc, recursively traverse and embedded class/structure based on an additional typedescription
    FIELD_CUSTOM,   // special type that contains function pointers to it's read/write/parse functions

    FIELD_CLASSPTR, // CBaseEntity *
    FIELD_EHANDLE,  // Entity handle
    FIELD_EDICT,    // edict_t *

    FIELD_POSITION_VECTOR, // A world coordinate (these are fixed up across level transitions automagically)
    FIELD_TIME,            // a floating point time (these are fixed up automatically too!)
    FIELD_TICK,            // an integer tick count( fixed up similarly to time)
    FIELD_MODELNAME,       // Engine string that is a model name (needs precache)
    FIELD_SOUNDNAME,       // Engine string that is a sound name (needs precache)

    FIELD_INPUT,    // a list of inputed data fields (all derived from CMultiInputVar)
    FIELD_FUNCTION, // A class function pointer (Think, Use, etc)

    FIELD_VMATRIX, // a vmatrix (output coords are NOT worldspace)

    // NOTE: Use float arrays for local transformations that don't need to be fixed up.
    FIELD_VMATRIX_WORLDSPACE, // A VMatrix that maps some local space to world space (translation is fixed up on level transitions)
    FIELD_MATRIX3X4_WORLDSPACE, // matrix3x4_t that maps some local space to world space (translation is fixed up on level transitions)

    FIELD_INTERVAL,      // a start and range floating point interval ( e.g., 3.2->3.6 == 3.2 and 0.4 )
    FIELD_MODELINDEX,    // a model index
    FIELD_MATERIALINDEX, // a material index (using the material precache string table)

    FIELD_VECTOR2D, // 2 floats

    FIELD_TYPECOUNT, // MUST BE LAST
} ch_field_type;

// This field is masked for global entity save/restore
#define FTYPEDESC_GLOBAL 0x0001
// This field is saved to disk
#define FTYPEDESC_SAVE 0x0002
// This field can be requested and written to by string name at load time
#define FTYPEDESC_KEY 0x0004
// This field can be written to by string name at run time, and a function called
#define FTYPEDESC_INPUT 0x0008
// This field propogates it's value to all targets whenever it changes
#define FTYPEDESC_OUTPUT 0x0010
// This is a table entry for a member function pointer
#define FTYPEDESC_FUNCTIONTABLE 0x0020
// This field is a pointer, not an embedded object
#define FTYPEDESC_PTR 0x0040
// The field is an override for one in a base class (only used by prediction system for now)
#define FTYPEDESC_OVERRIDE 0x0080

// Flags used by other systems (e.g., prediction system)

// This field is present in a network SendTable
#define FTYPEDESC_INSENDTABLE 0x0100
// The field is local to the client or server only (not referenced by prediction code and not replicated by networking)
#define FTYPEDESC_PRIVATE 0x0200
// The field is part of the prediction typedescription, but doesn't get compared when checking for errors
#define FTYPEDESC_NOERRORCHECK 0x0400
// The field is a model index (used for debugging output)
#define FTYPEDESC_MODELINDEX 0x0800
// The field is an index into file data, used for byteswapping.
#define FTYPEDESC_INDEX 0x1000

// These flags apply to C_BasePlayer derived objects only

// By default you can only view fields on the local player (yourself),
// but if this is set, then we allow you to see fields on other players
#define FTYPEDESC_VIEW_OTHER_PLAYER 0x2000
// Only show this data if the player is on the same team as the local player
#define FTYPEDESC_VIEW_OWN_TEAM 0x4000
// Never show this field to anyone, even the local player (unusual)
#define FTYPEDESC_VIEW_NEVER 0x8000

struct ch_datamap;
struct ch_type_description;
struct ch_byte_reader;
struct ch_byte_writer;
enum ch_err;

enum {
    TD_OFFSET_NORMAL = 0,
    TD_OFFSET_PACKED = 1,

    // Must be last
    TD_OFFSET_COUNT,
};

typedef struct typedescription_t {
    ch_field_type fieldType;
    const char* fieldName;
    int fieldOffset[TD_OFFSET_COUNT]; // 0 == normal, 1 == packed offset
    unsigned short fieldSize;
    short flags;
    // the name of the variable in the map/fgd data, or the name of the action
    const char* externalName;
    // pointer to the function set for save/restoring of custom data types
    void* pSaveRestoreOps;
    // for associating function with string names
    void* inputFunc;
    // For embedding additional datatables inside this one
    struct datamap_t* td;

    // Stores the actual member variable size in bytes
    int fieldSizeInBytes;

    // FTYPEDESC_OVERRIDE point to first baseclass instance if chains_validated has occurred
    struct typedescription_t* override_field;

    // Used to track exclusion of baseclass fields
    int override_count;

    // Tolerance for field errors for float fields
    float fieldTolerance;
} typedescription_t;

typedef struct datamap_t {
    struct typedescription_t* dataDesc;
    int dataNumFields;
    char const* dataClassName;
    struct datamap_t* baseMap;

    bool chains_validated;
    // Have the "packed" offsets been computed
    bool packed_offsets_computed;
    char _pad[2];
    int packed_size;
} datamap_t;

// TOOD this is a circular ref to the enum - the header files could be better organized!
typedef enum ch_err (*ch_save_restore_op_calc_restored_size)(const struct ch_type_description* td,
                                                             struct ch_byte_reader* br);

typedef enum ch_err (*ch_save_restore_op_restore)(const struct ch_type_description* td,
                                                  struct ch_byte_reader* br,
                                                  struct ch_byte_writer* bw);

typedef struct ch_save_restore_ops {
    // ch_save_restore_op_calc_restored_size calc_restored_size;
    // ch_save_restore_op_restore restore;
    int x;
} ch_save_restore_ops;

// when written to file, structures will be saved relative to some point in the file;
// when loaded they'll be fixed up to just be the raw pointers
#define CH_PACKED_PTR(ptr_type, name) \
    union {                           \
        ptr_type name;                \
        size_t name##_rel_off;        \
    }

// update whenever changes are made to ch_type_description or ch_datamap
#define CH_DATAMAP_STRUCT_VERSION 1

#define CH_REL_OFF_NULL SIZE_MAX

// a slightly modified version of the game's datamap_t
typedef struct ch_datamap {
    CH_PACKED_PTR(const char*, class_name);
    CH_PACKED_PTR(const struct ch_datamap*, base_map);
    CH_PACKED_PTR(const struct ch_type_description*, fields);
    size_t n_fields;
    // necessary to keep this to uniquely determine restore ops (in case of collisions across modules)
    CH_PACKED_PTR(const char*, module_name);
} ch_datamap;

// a slightly modified version of the game's typedescription_t
typedef struct ch_type_description {
    ch_field_type type;
    unsigned short flags;
    char _pad[2];
    CH_PACKED_PTR(const char*, name);
    CH_PACKED_PTR(const char*, external_name);
    size_t off;
    CH_PACKED_PTR(const struct ch_save_restore_ops*, save_restore_ops);
    CH_PACKED_PTR(const struct ch_datamap*, embedded_map);
} ch_type_description;

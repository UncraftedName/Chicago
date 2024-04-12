import dataclasses
from typing import Union, List, Optional
import enum
import jsonschema

HEADER_KV_CH_VERSION = 'chicago_version'
HEADER_KV_GAME_NAME = 'game_name'
HEADER_KV_GAME_VERSION = 'game_version'
HEADER_KV_DATAMAPS = 'datamaps'

DM_KV_NAME = 'name'
DM_KV_MODULE = 'module'
DM_KV_MODULE_OFF = 'module_offset'
DM_KV_BASE = 'base_map'
DM_KV_FIELDS = 'fields'

TD_KV_NAME = 'name'
TD_KV_TYPE = 'type'
TD_KV_OFFSET = 'offset'
TD_KV_FLAGS = 'flags'
TD_KV_EXTERNAL_NAME = 'external_name'
TD_KV_TOTAL_SIZE = 'total_size_bytes'
TD_KV_INPUT_FUNC = 'input_func'
TD_KV_RESTORE_OPS = 'restore_ops'
TD_KV_EMBEDDED = 'embedded_map'
TD_KV_OVERRIDE_COUNT = 'override_count'
TD_KV_FLOAT_TOL = 'float_tolerance'


class FIELD_TYPE(enum.IntEnum):
    VOID = 0
    FLOAT = enum.auto()
    STRING = enum.auto()
    VECTOR = enum.auto()
    QUAT = enum.auto()
    INT = enum.auto()
    BOOL = enum.auto()
    SHORT = enum.auto()
    CHAR = enum.auto()
    COLOR32 = enum.auto()
    EMBEDDED = enum.auto()
    CUSTOM = enum.auto()
    CLASS_PTR = enum.auto()
    EHANDLE = enum.auto()
    EDICT = enum.auto()
    POS_VECTOR = enum.auto()
    TIME = enum.auto()
    TICK = enum.auto()
    MODEL_NAME = enum.auto()
    SOUND_NAME = enum.auto()
    INPUT = enum.auto()
    FUNCTION = enum.auto()
    VMATRIX = enum.auto()
    VMATRIX_WORLDSPACE = enum.auto()
    MATRIX3X4_WORLDSPACE = enum.auto()
    FLOAT_INTERVAL = enum.auto()
    MODEL_INDEX = enum.auto()
    MATERIAL_INDEX = enum.auto()
    VECTOR2D = enum.auto()


# these dataclasses must have the same field names as the keys


@dataclasses.dataclass
class TypeDesc_V2:
    name: str
    type: FIELD_TYPE
    flags: int
    external_name: Optional[str]
    offset: int
    total_size_bytes: int
    input_func: Optional[int]
    restore_ops: Optional[int]
    embedded_map: Union[None, str, 'DataMap_V2']
    override_count: int
    float_tolerance: float


@dataclasses.dataclass
class DataMap_V2:
    name: str
    module: str
    module_offset: int
    base_map: Union[None, str, 'DataMap_V2']
    fields: List[TypeDesc_V2]


@dataclasses.dataclass
class DataMapSaveFile_V2:
    chicago_version: int
    game_name: str
    game_version: str
    datamaps: List[DataMap_V2]


# assumes the input object has already been validated_against the schema
def save_object_to_dataclass_v2(obj: dict, recurse_unpack_datamaps: bool) -> DataMapSaveFile_V2:
    ret = DataMapSaveFile_V2(**obj)
    ret.datamaps = [DataMap_V2(**dm) for dm in ret.datamaps]
    dm_lookup = {dm.name: dm for dm in ret.datamaps}
    for dm in ret.datamaps:
        dm.fields = [TypeDesc_V2(**td) for td in dm.fields]
        # manually covert the type to the enum class, idk if there's a better way to do this
        for f in dm.fields:
            f.type = FIELD_TYPE(f.type)
        if recurse_unpack_datamaps:
            dm.base_map = dm_lookup.get(dm.base_map, dm.base_map)
            for td in dm.fields:
                td.embedded_map = dm_lookup.get(td.embedded_map, td.embedded_map)
    return ret


_datamap_object_schema_v2 = {
    'type': 'object',
    'additionalProperties': False,
    'required': [HEADER_KV_CH_VERSION, HEADER_KV_GAME_NAME, HEADER_KV_GAME_VERSION, HEADER_KV_DATAMAPS],
    'properties': {
            HEADER_KV_CH_VERSION: {'const': 2},
            HEADER_KV_GAME_NAME: {'type': 'string'},
            HEADER_KV_GAME_VERSION: {'type': 'string'},
            HEADER_KV_DATAMAPS: {
                'type': 'array',
                'minItems': 1,
                'items': {
                    'type': 'object',
                    'additionalProperties': False,
                    'required': [DM_KV_NAME, DM_KV_MODULE, DM_KV_MODULE_OFF, DM_KV_BASE, DM_KV_FIELDS],
                    'properties': {
                        DM_KV_NAME: {'type': 'string'},
                        DM_KV_MODULE: {'type': 'string'},
                        DM_KV_MODULE_OFF: {'type': 'integer'},
                        DM_KV_BASE: {'$ref': '#/$defs/optional_string'},
                        DM_KV_FIELDS: {
                            'type': 'array',
                            'items': {
                                'type': 'object',
                                'required': [
                                    TD_KV_NAME,
                                    TD_KV_TYPE,
                                    TD_KV_OFFSET,
                                    TD_KV_FLAGS,
                                    TD_KV_EXTERNAL_NAME,
                                    TD_KV_TOTAL_SIZE,
                                    TD_KV_INPUT_FUNC,
                                    TD_KV_RESTORE_OPS,
                                    TD_KV_EMBEDDED,
                                    TD_KV_OVERRIDE_COUNT,
                                    TD_KV_FLOAT_TOL,
                                ],
                                'additionalProperties': False,
                                'properties': {
                                    TD_KV_NAME: {'type': 'string'},
                                    TD_KV_TYPE: {'type': 'integer', 'minimum': 0, 'exclusiveMaximum': len(FIELD_TYPE)},
                                    TD_KV_OFFSET: {'$ref': '#/$defs/optional_offset'},
                                    TD_KV_FLAGS: {'type': 'integer'},
                                    TD_KV_EXTERNAL_NAME: {'$ref': '#/$defs/optional_string'},
                                    TD_KV_TOTAL_SIZE: {'$ref': '#/$defs/non_negative_int'},
                                    TD_KV_INPUT_FUNC: {'$ref': '#/$defs/optional_offset'},
                                    TD_KV_RESTORE_OPS: {'$ref': '#/$defs/optional_offset'},
                                    TD_KV_EMBEDDED: {'$ref': '#/$defs/optional_string'},
                                    TD_KV_OVERRIDE_COUNT: {'$ref': '#/$defs/non_negative_int'},
                                    TD_KV_FLOAT_TOL: {'type': 'number'},
                                },
                            },
                        }
                    }
                }
            }
    },
    '$defs': {
        'non_negative_int': {
            "type": "integer",
            "minimum": 0
        },
        'optional_offset': {
            'oneOf': [
                {'type': 'null'},
                {'$ref': '#/$defs/non_negative_int'},
            ]
        },
        'optional_string': {
            'oneOf': [
                {'type': 'null'},
                {'type': 'string'},
            ]
        },
    }
}

jsonschema.Draft202012Validator.check_schema(_datamap_object_schema_v2)
_datamap_object_validator_v2 = jsonschema.Draft202012Validator(_datamap_object_schema_v2)


def validate_datamap_save_schema_v2(dm_so: object) -> None:
    _datamap_object_validator_v2.validate(dm_so)
    visited_maps = {None}
    # check that all datamaps any single one depends on came before it in the list
    for dm in dm_so[HEADER_KV_DATAMAPS]:
        dependency_maps = {td[TD_KV_EMBEDDED] for td in dm[DM_KV_FIELDS]} | {dm[DM_KV_BASE]}
        assert dependency_maps.issubset(visited_maps)
        visited_maps.add(dm[DM_KV_NAME])

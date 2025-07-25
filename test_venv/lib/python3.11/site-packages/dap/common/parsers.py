import base58
import hashlib
from typing import Tuple


def parse_cf_v1_address(address: str) -> Tuple[int, int, int, bytes, bytes, bytes]:
    """
    Parse a CF v1 format address and returns its various components.

    Args:
        address (str): The CF v1 address string to parse. It should be a Base58 encoded string.

    Returns:
        tuple: A tuple containing the following components:
            - version (int): The address version.
            - net_id (int): The ID of the network to which this address belongs.
            - sign_id (int): The signature ID this wallet uses.
            - public_hash (bytes): The public hash. The main identifier of the wallet on the network.
            - summary_hash (bytes): The summary hash.
            - control_hash (bytes): The control hash.

    Raises:
        ValueError: If the address is invalid.
    """
    bdata: bytes = base58.b58decode(address)
    version = bdata[0]
    net_id = int.from_bytes((bdata[1:9]), byteorder='little')
    sign_id = int.from_bytes((bdata[9:13]), byteorder='little')
    public_hash = bdata[13:45]
    control_hash = bdata[45:]
    hash = hashlib.sha3_256()
    hash.update(bdata[:45])
    summary_hash = hash.digest()
    if summary_hash != control_hash:
        raise ValueError(f"Address={address} not valid")
    return version, net_id, sign_id, public_hash, summary_hash, control_hash

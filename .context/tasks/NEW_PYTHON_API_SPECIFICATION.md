# 🚀 **New Python Cellframe SDK - API Specification**

## 📋 **Overview**

Новый Python Cellframe SDK предоставляет современный, pythonic интерфейс для работы с блокчейном Cellframe. API следует принципам:

- **Type Safety**: Полная поддержка type hints
- **Context Managers**: Автоматическое управление ресурсами
- **Async Support**: Native async/await для сетевых операций
- **Fluent API**: Chainable методы для сложных операций
- **Error Handling**: Specific exceptions с контекстом

---

## 🏗️ **Core Module (cellframe.core)**

### CellframeNode - Main Entry Point

```python
from cellframe.core import CellframeNode, NodeConfig
from cellframe.types import NetworkID

class CellframeNode:
    """Main entry point for Cellframe SDK operations."""
    
    @classmethod
    def create(
        cls,
        network: str = 'mainnet',
        config: Optional[NodeConfig] = None,
        data_dir: Optional[Path] = None
    ) -> 'CellframeNode':
        """Create a new node instance.
        
        Args:
            network: Network to connect to ('mainnet', 'testnet', 'private')
            config: Optional custom configuration
            data_dir: Directory for node data storage
            
        Returns:
            Configured CellframeNode instance
            
        Example:
            >>> node = CellframeNode.create(network='testnet')
            >>> await node.start()
        """
    
    async def start(self) -> None:
        """Start the node and establish network connections."""
    
    async def stop(self) -> None:
        """Stop the node and cleanup resources."""
    
    def get_crypto(self) -> 'CryptoModule':
        """Get crypto operations module."""
    
    def get_network(self) -> 'NetworkModule':
        """Get network operations module."""
    
    def get_chain(self) -> 'ChainModule':
        """Get blockchain operations module."""
    
    def get_wallet(self) -> 'WalletModule':
        """Get wallet operations module."""

# Usage Examples:
node = CellframeNode.create(network='testnet')
crypto = node.get_crypto()
network = node.get_network()
```

### Context Managers

```python
from cellframe.core import PluginContext

# Standalone mode
with CellframeNode.create(network='mainnet') as node:
    balance = await node.get_balance(wallet_address)

# Plugin mode  
async with PluginContext() as context:
    node = context.get_node()
    result = await node.submit_transaction(tx)
```

---

## 🔐 **Crypto Module (cellframe.crypto)**

### Key Management

```python
from cellframe.crypto import CryptoKey, KeyType, Signature
from typing import Optional

class CryptoKey:
    """Cryptographic key with automatic resource management."""
    
    @classmethod
    def generate(cls, key_type: KeyType) -> 'CryptoKey':
        """Generate a new cryptographic key.
        
        Args:
            key_type: Type of key to generate (ECDSA, RSA, Ed25519)
            
        Returns:
            New CryptoKey instance
            
        Example:
            >>> with CryptoKey.generate(KeyType.ECDSA_SECP256K1) as key:
            ...     signature = key.sign(data)
        """
    
    @classmethod
    def from_file(cls, path: Path, password: Optional[str] = None) -> 'CryptoKey':
        """Load key from file."""
    
    @classmethod  
    def from_bytes(cls, data: bytes, key_type: KeyType) -> 'CryptoKey':
        """Create key from raw bytes."""
    
    def sign(self, data: bytes) -> Signature:
        """Sign data with this key.
        
        Args:
            data: Data to sign
            
        Returns:
            Digital signature
            
        Raises:
            CryptoException: If signing fails
        """
    
    def verify(self, data: bytes, signature: Signature) -> bool:
        """Verify signature against data."""
    
    def get_public_key(self) -> 'PublicKey':
        """Get corresponding public key."""
    
    def to_bytes(self) -> bytes:
        """Export key as bytes."""
    
    def save_to_file(self, path: Path, password: Optional[str] = None) -> None:
        """Save key to encrypted file."""
    
    # Context manager protocol
    def __enter__(self) -> 'CryptoKey':
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self._cleanup()

# Usage Examples:
with CryptoKey.generate(KeyType.ECDSA_SECP256K1) as key:
    signature = key.sign(b"Hello World")
    public_key = key.get_public_key()
    is_valid = public_key.verify(b"Hello World", signature)
```

### Hashing

```python
from cellframe.crypto import Hasher, HashAlgorithm, Hash

class Hasher:
    """Cryptographic hashing with context manager support."""
    
    @classmethod
    def create(cls, algorithm: HashAlgorithm) -> 'Hasher':
        """Create a new hasher instance."""
    
    @staticmethod
    def hash_data(data: bytes, algorithm: HashAlgorithm = HashAlgorithm.SHA256) -> Hash:
        """Quick hash of data.
        
        Example:
            >>> hash_result = Hasher.hash_data(b"Hello World")
            >>> print(hash_result.hex())
        """
    
    def update(self, data: bytes) -> 'Hasher':
        """Add data to hash (chainable)."""
    
    def finalize(self) -> Hash:
        """Finalize and get hash result."""

# Usage Examples:
# Quick hashing
hash_result = Hasher.hash_data(b"Hello World", HashAlgorithm.SHA256)

# Incremental hashing
with Hasher.create(HashAlgorithm.SHA3_256) as hasher:
    hash_result = (hasher
                   .update(b"Hello")
                   .update(b" ")
                   .update(b"World")
                   .finalize())
```

---

## 🌐 **Network Module (cellframe.network)**

### Async Network Client

```python
from cellframe.network import NetworkClient, PeerInfo
from cellframe.types import Address, TransactionHash, BlockHash

class NetworkClient:
    """Async network client for blockchain operations."""
    
    @classmethod
    async def connect(cls, network: str) -> 'NetworkClient':
        """Connect to network.
        
        Example:
            >>> async with NetworkClient.connect('mainnet') as client:
            ...     balance = await client.get_balance(address)
        """
    
    async def get_balance(self, address: Address) -> TokenAmount:
        """Get account balance.
        
        Args:
            address: Wallet address to check
            
        Returns:
            Current balance
            
        Raises:
            NetworkException: If network request fails
        """
    
    async def get_transaction(self, tx_hash: TransactionHash) -> Optional[Transaction]:
        """Get transaction by hash."""
    
    async def submit_transaction(self, transaction: Transaction) -> TransactionHash:
        """Submit transaction to network."""
    
    async def get_block(self, block_hash: BlockHash) -> Optional[Block]:
        """Get block by hash."""
    
    async def get_latest_block(self) -> Block:
        """Get latest block."""
    
    async def subscribe_to_events(
        self, 
        event_filter: EventFilter
    ) -> AsyncIterator[BlockchainEvent]:
        """Subscribe to blockchain events.
        
        Example:
            >>> async for event in client.subscribe_to_events(filter):
            ...     print(f"New transaction: {event.transaction_hash}")
        """
    
    # Async context manager
    async def __aenter__(self) -> 'NetworkClient':
        await self.connect()
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.disconnect()

# Usage Examples:
async with NetworkClient.connect('mainnet') as client:
    # Check balance
    balance = await client.get_balance(my_address)
    
    # Submit transaction
    tx_hash = await client.submit_transaction(my_transaction)
    
    # Subscribe to events
    event_filter = EventFilter(addresses=[my_address])
    async for event in client.subscribe_to_events(event_filter):
        print(f"New event: {event}")
```

---

## ⛓️ **Chain Module (cellframe.chain)**

### Transaction Building

```python
from cellframe.chain import TransactionBuilder, Transaction
from cellframe.types import Address, TokenAmount

class TransactionBuilder:
    """Fluent API for building transactions."""
    
    def from_address(self, address: Address) -> 'TransactionBuilder':
        """Set sender address (chainable)."""
    
    def to_address(self, address: Address) -> 'TransactionBuilder':
        """Set recipient address (chainable)."""
    
    def amount(self, value: TokenAmount) -> 'TransactionBuilder':
        """Set transfer amount (chainable)."""
    
    def fee(self, value: TokenAmount) -> 'TransactionBuilder':
        """Set transaction fee (chainable)."""
    
    def data(self, payload: bytes) -> 'TransactionBuilder':
        """Set transaction data (chainable)."""
    
    def build(self) -> Transaction:
        """Build unsigned transaction."""
    
    def build_and_sign(self, private_key: CryptoKey) -> Transaction:
        """Build and sign transaction."""

# Usage Examples:
transaction = (TransactionBuilder()
               .from_address(my_address)
               .to_address(recipient_address)
               .amount(TokenAmount('1.5', 'CELL'))
               .fee(TokenAmount('0.001', 'CELL'))
               .build_and_sign(my_private_key))

# Submit to network
async with NetworkClient.connect('mainnet') as client:
    tx_hash = await client.submit_transaction(transaction)
    print(f"Transaction submitted: {tx_hash}")
```

### Smart Contracts

```python
from cellframe.chain import SmartContract, ContractABI

class SmartContract:
    """Smart contract interaction."""
    
    @classmethod
    async def deploy(
        cls,
        bytecode: bytes,
        abi: ContractABI,
        constructor_args: List[Any],
        deployer_key: CryptoKey
    ) -> 'SmartContract':
        """Deploy new smart contract."""
    
    @classmethod
    def at_address(cls, address: Address, abi: ContractABI) -> 'SmartContract':
        """Connect to existing contract."""
    
    async def call_function(
        self,
        function_name: str,
        *args,
        caller_key: Optional[CryptoKey] = None
    ) -> Any:
        """Call contract function."""
    
    async def estimate_gas(self, function_name: str, *args) -> int:
        """Estimate gas for function call."""

# Usage Examples:
# Deploy contract
contract = await SmartContract.deploy(
    bytecode=contract_bytecode,
    abi=contract_abi,
    constructor_args=[initial_value],
    deployer_key=my_key
)

# Call function
result = await contract.call_function('getValue')
await contract.call_function('setValue', 42, caller_key=my_key)
```

---

## 💰 **Wallet Module (cellframe.wallet)**

### Wallet Management

```python
from cellframe.wallet import Wallet, WalletManager
from cellframe.types import Mnemonic

class Wallet:
    """Secure wallet implementation."""
    
    @classmethod
    def create_new(cls, password: str) -> Tuple['Wallet', Mnemonic]:
        """Create new wallet with mnemonic.
        
        Returns:
            Tuple of (wallet, mnemonic_phrase)
        """
    
    @classmethod
    def from_mnemonic(cls, mnemonic: Mnemonic, password: str) -> 'Wallet':
        """Restore wallet from mnemonic."""
    
    @classmethod
    def from_file(cls, path: Path, password: str) -> 'Wallet':
        """Load wallet from file."""
    
    def get_address(self, index: int = 0) -> Address:
        """Get wallet address by index."""
    
    def get_private_key(self, index: int = 0) -> CryptoKey:
        """Get private key by index."""
    
    async def get_balance(self, token: Optional[Token] = None) -> TokenAmount:
        """Get wallet balance."""
    
    async def send(
        self,
        to_address: Address,
        amount: TokenAmount,
        fee: Optional[TokenAmount] = None
    ) -> TransactionHash:
        """Send tokens to address."""
    
    def save_to_file(self, path: Path) -> None:
        """Save encrypted wallet to file."""

# Usage Examples:
# Create new wallet
wallet, mnemonic = Wallet.create_new(password="secure_password")
print(f"Save this mnemonic: {mnemonic}")

# Get address and balance
address = wallet.get_address()
balance = await wallet.get_balance()

# Send transaction
tx_hash = await wallet.send(
    to_address=recipient_address,
    amount=TokenAmount('1.0', 'CELL')
)
```

---

## 🔧 **Services Module (cellframe.services)**

### Staking Service

```python
from cellframe.services import StakingService, StakingOrder

class StakingService:
    """High-level staking operations."""
    
    def create_stake_order(self) -> 'StakingOrderBuilder':
        """Create new staking order builder."""
    
    async def get_validators(self) -> List[ValidatorInfo]:
        """Get list of available validators."""
    
    async def get_staking_rewards(self, address: Address) -> TokenAmount:
        """Get accumulated staking rewards."""

class StakingOrderBuilder:
    """Fluent API for staking orders."""
    
    def validator(self, validator_address: Address) -> 'StakingOrderBuilder':
        """Select validator (chainable)."""
    
    def amount(self, stake_amount: TokenAmount) -> 'StakingOrderBuilder':
        """Set stake amount (chainable)."""
    
    def duration(self, days: int) -> 'StakingOrderBuilder':
        """Set staking duration (chainable)."""
    
    async def execute(self, wallet: Wallet) -> TransactionHash:
        """Execute staking order."""

# Usage Examples:
staking_service = StakingService()

# Create and execute staking order
tx_hash = await (staking_service
                .create_stake_order()
                .validator(validator_address)
                .amount(TokenAmount('1000', 'CELL'))
                .duration(days=30)
                .execute(my_wallet))
```

---

## 🔄 **Legacy Compatibility (cellframe.legacy)**

### Backward Compatibility Layer

```python
from cellframe.legacy import LegacyAPI

class LegacyAPI:
    """Backward compatibility with old API."""
    
    @staticmethod
    def init(json_config: str) -> None:
        """Legacy init function - automatically migrated."""
        # Automatically converts to new API
        config = LegacyAPI._migrate_config(json_config)
        node = CellframeNode.create(config=config)
        _legacy_node_instance = node
    
    @staticmethod
    def deinit() -> None:
        """Legacy deinit - handled automatically."""
        pass

# Legacy code still works:
from CellFrame import *  # Old import
init(json_string)        # Old API call
# Automatically uses new implementation under the hood
```

---

## 🛠️ **Configuration & Error Handling**

### Configuration

```python
from cellframe.config import NodeConfig
from pathlib import Path

config = NodeConfig(
    network='testnet',
    data_dir=Path.home() / '.cellframe',
    log_level='INFO',
    max_connections=50,
    enable_mining=False
)

node = CellframeNode.create(config=config)
```

### Error Handling

```python
from cellframe.exceptions import (
    CellframeException,
    NetworkException, 
    CryptoException,
    ValidationException
)

try:
    async with NetworkClient.connect('mainnet') as client:
        balance = await client.get_balance(address)
except NetworkException as e:
    print(f"Network error: {e.message}")
    print(f"Error code: {e.error_code}")
    print(f"Peer info: {e.peer_info}")
except ValidationException as e:
    print(f"Validation failed: {e.validation_errors}")
except CellframeException as e:
    print(f"General error: {e}")
```

---

## 🚀 **Quick Start Examples**

### Complete Example: Send Transaction

```python
import asyncio
from cellframe import CellframeNode
from cellframe.types import Address, TokenAmount

async def send_transaction_example():
    # Create node
    node = CellframeNode.create(network='testnet')
    
    # Create wallet
    wallet, mnemonic = node.get_wallet().create_new(password="secure")
    print(f"Wallet address: {wallet.get_address()}")
    
    # Get network client
    async with node.get_network().connect() as client:
        # Check balance
        balance = await client.get_balance(wallet.get_address())
        print(f"Balance: {balance}")
        
        # Send transaction
        if balance.value > 0:
            tx_hash = await wallet.send(
                to_address=Address.from_string("0x742d35..."),
                amount=TokenAmount('0.1', 'CELL')
            )
            print(f"Transaction sent: {tx_hash}")

if __name__ == "__main__":
    asyncio.run(send_transaction_example())
```

### Complete Example: Smart Contract Interaction

```python
async def smart_contract_example():
    node = CellframeNode.create(network='testnet')
    
    with CryptoKey.generate(KeyType.ECDSA_SECP256K1) as deployer_key:
        # Deploy contract
        contract = await SmartContract.deploy(
            bytecode=my_contract_bytecode,
            abi=my_contract_abi,
            constructor_args=[100],
            deployer_key=deployer_key
        )
        
        # Interact with contract
        current_value = await contract.call_function('getValue')
        print(f"Current value: {current_value}")
        
        # Update value
        await contract.call_function(
            'setValue', 
            200, 
            caller_key=deployer_key
        )
        
        new_value = await contract.call_function('getValue')
        print(f"New value: {new_value}")
```

---

## 📊 **Type Definitions**

```python
from typing import Union, Optional, List, Dict, Any
from decimal import Decimal
from datetime import datetime
from pathlib import Path

# Core Types
NetworkID = str
Address = str
TransactionHash = str  
BlockHash = str
Mnemonic = str

# Token Types
class TokenAmount:
    value: Decimal
    symbol: str

# Event Types  
class BlockchainEvent:
    event_type: str
    block_number: int
    transaction_hash: Optional[TransactionHash]
    timestamp: datetime
    data: Dict[str, Any]
```

---

## 🎯 **Migration Guide**

### From Old API to New API

| Old API | New API |
|---------|---------|
| `init(json_config)` | `CellframeNode.create(network='mainnet')` |
| `Crypto.newKey(type)` | `CryptoKey.generate(KeyType.ECDSA)` |
| `del key` | `with CryptoKey.generate() as key:` |
| Manual config | Type-safe configuration objects |
| Sync operations | Async/await for network operations |

---

**🎉 This API specification provides the foundation for a modern, pythonic Cellframe SDK that follows best practices while maintaining full backward compatibility.** 
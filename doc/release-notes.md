# Bitcoin SV Node Software – v1.2.0 Release

This release is a hard fork which updates the BSV protocol.   

The scheduled TestNet activation height is 1,621,670 (Target is 12:00 midday 31-Oct-2024)

The scheduled MainNet activation height is 882,687 (Target is 12:00 midday 4-Feb-2025)

**What’s changed**  

The following opcodes are restored:  

*   **OP\_VER** – pushes the transaction version (4 byte array) onto the top of the stack.
*   **OP\_VERIF, OP\_VERNOTIF** – provides conditional logic based on the transaction version.
*   **OP\_SUBSTR** – creates an arbitrary substring of the string on top of the stack.
*   **OP\_LEFT, OP\_RIGHT –** creates the left/right most substring of specific length from the string on the top of the stack respectively.
*   **OP\_2MUL** – doubles the number on top of the stack
*   **OP\_2DIV** – halves the number on top of the stack

Please refer to the Protocol Restoration specification for further details.  

The limitations in the following areas are removed.   

*   **Script numbers** – Limits on the maximum size of script numbers are removed. Please note that practical limits are imposed by the external libraries used to implement script numbers (max size is currently 64MB)
*   **Minimal Encoding Requirement** – Previously releases required transactions to encode numbers as efficiently as possible. For example, the number two can be encoded as 0002 (2 bytes) or 02 (1 byte). Prior to this release only the second version is accepted. Minimal encoding places an unnecessary burden on users that was not present in the original Satoshi implementation and it is removed from this release.
*   **Low S signatures -** If S is a signature, then so is -S. The “low S” signature requirement was introduced to decrease transaction malleability but is an unnecessary requirement and is removed. Either signature S or -S can now be used.
*   **Clean Stack Policy** – Previous releases required that after the execution of the unlocking and locking script, the stack is “clean”. i.e. there is only a single item (interpreted as true/false) on the stack that indicates whether the transaction has permission to spend the relevant input. The requirement is unnecessary and adds complexity to scripts. This release removes the clean stack requirement.
*   **No opcodes in Unlocking Scripts Policy-** Previous releases do not allow unlocking scripts to include non-data opcodes. That restriction is removed in this release.

## Other items

*   STN Reset - includes an updated chain height block hash.


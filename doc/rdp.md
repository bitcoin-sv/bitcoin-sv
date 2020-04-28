Responsible Disclosure Policy
=============================

Please check the [Bitcoin SV website](https://bitcoinsv.io/security/) for the most up-to-date version of this policy.

Introduction
------------
Security is core to our values, and we value the input of security researchers acting in good faith to help us maintain 
high standards of security and privacy for our users and the Bitcoin SV blockchain. This includes encouraging 
responsible vulnerability research and disclosure. This policy sets out our definition of good faith in the context of 
finding and reporting vulnerabilities, as well as what you can expect from us in return.

We may modify the terms of this policy or terminate this policy at any time. We won’t apply any changes we make to 
these policy terms retroactively.

Expectations
------------
When working with us according to this policy, you can expect us to:

* Extend Safe Harbor for your vulnerability research that is related to this policy;
* Work with you to understand and validate your report, including an initial response to the report within 72 hours of 
  submission;
* Work to remediate discovered vulnerabilities in a timely manner; and
* Recognize your contribution to improving our security if you are the first to report a unique vulnerability, and your 
  report triggers a code or configuration change.
  
Safe Harbor
-----------
When conducting vulnerability research according to this policy, we consider this research to be:

* Authorized in accordance with the Computer Fraud and Abuse Act (CFAA) (and/or similar state laws), and we will not 
  initiate or support legal action against you for accidental, good faith violations of this policy;
* Exempt from the Digital Millennium Copyright Act (DMCA), and we will not bring a claim against you for circumvention 
  of technology controls;
* Exempt from restrictions in our Terms & Conditions that would interfere with conducting security research, and we 
  waive those restrictions on a limited basis for work done under this policy;
* Lawful, helpful to the overall security of the Internet, and conducted in good faith.

You are expected, as always, to comply with all applicable laws.

If at any time you have concerns or are uncertain whether your security research is consistent with this policy, please 
submit a report through one of our Official Channels before going any further.

Ground Rules
------------
To encourage vulnerability research and to avoid any confusion between good-faith research and malicious attack, we ask 
that you:

* Play by the rules. This includes following this policy, as well as any other relevant agreements. If there is any 
  inconsistency between this policy and any other relevant terms, the terms of this policy will prevail.
* Report any vulnerability you’ve discovered promptly.
* Avoid violating the privacy of others, disrupting our systems, destroying data, and/or harming user experience.
* Use only the Official Channels to discuss vulnerability information with us.
* Keep the details of any discovered vulnerabilities confidential until they are fixed, according to the Disclosure 
  Terms in this policy.
* Perform testing only on in-scope systems, and respect systems and activities which are out-of-scope.
* If a vulnerability provides unintended access to data: Limit the amount of data you access to the minimum required 
  for effectively demonstrating a Proof of Concept; and cease testing and submit a report immediately if you encounter 
  any user data during testing, such as Personally Identifiable Information (PII), or proprietary information.
* You should only interact with test accounts you own or with explicit permission from the account holder.
* Do not engage in extortion.

Official Channels
-----------------
To help us receive vulnerability submissions we use the following official reporting channel:

* email: security@bitcoinsv.io

If you think you’ve found a vulnerability, please include the following details with your report and be as descriptive 
as possible:

* The location and nature of the vulnerability,
* A detailed description of the steps required to reproduce the vulnerability (screenshots, compressed screen 
  recordings, and proof-of-concept scripts are all helpful), and
* Your name/handle and a link for recognition.

Please encrypt all information that you send to us using our PGP key (security@bitcoinsv.io). This key is available from 
Public PGP Key Servers such as the [MIT PGP Public Key Server](https://pgp.mit.edu/). The PGP key has ID `7A20AB62` and 
fingerprint `E8EB 970A 1C60 7DF0 822E 1388 F969 76FD 7A20 AB62`. The PGP key is included in its entirety at the
bottom of this page for your convenience.

Rewards
-------
A ‘bounty’ or reward may be payable for the responsible disclosure of vulnerabilities in accordance with our policy and 
ground rules, and provided that the Bitcoin SV security team is one of the original recipients of the disclosure.

The final amount is always chosen at the discretion of the reward panel, but the general guidelines below provides 
examples of the maximum rewards that may be payable based on the severity of the vulnerability that has been found. 
It should be noted that only vulnerabilities that are economically feasible will be considered e.g. 51% attacks on the 
network will be considered economically unviable. 

| Severity	| Critical	| High	| Medium	| Low |
| --------  | --------- | ----- | --------- | --- |
| Description | Catastrophic impact on the network as a whole; network availability compromised; loss of funds | Impacts individual nodes; individual node crashes; potential for a loss of funds | Not easily exploitable; medium impact; no loss of funds | Not easily exploitable; low impact |
| Reward*	| $100,000 USD | $50,000 USD | $10,000 USD | $1,000 USD |
 

*All rewards will be paid out in Bitcoin SV from CoinGeek Mining’s open source budget.

Scope
-----
Our bug bounty policy focuses on the code base for Bitcoin SV and spans end-to-end: from soundness of protocols (such 
as the blockchain consensus model, the wire and p2p protocols, proof of work, etc.), protocol implementation and 
compliance to network security and consensus integrity. Classical client security as well as security of cryptographic 
primitives are also part of the policy.

Scope is limited to code contained in specified branches of the repository located at: 
[https://github.com/bitcoin-sv/bitcoin-sv](https://github.com/bitcoin-sv/bitcoin-sv). 
Branches in and out of scope are specified by the branch name:

Branches in scope:

* master branch
* most recently updated branch with prefix: rc-*
* branches prefixed with: review-*

Branches out of scope:

* branches prefixed with: dev-*, exp-* or research-*
* all other branches not specified as in scope

The scope of this policy is limited to those Operating Systems & hardware platforms for which binaries are released
by the Bitcoin SV Node implementation team. 
 

Out-of-scope
------------

* Findings from physical testing such as office access (e.g. open doors, tailgating)
* Findings derived primarily from social engineering (e.g. phishing, vishing)
* Findings from applications or systems not listed in the ‘Scope’ section
* Findings that have already been reported
* UI bugs and spelling mistakes on this or any associated website
* Network level Denial of Service (DoS/DDoS) vulnerabilities
* Resource exhaustion attacks subject to further caveats detailed below

Resource exhaustion attacks out of scope
----------------------------------------

We define "resource exhaustion attack" as an exploit designed to consume large amounts of CPU, memory, bandwidth or 
storage resources whether by normal operation of the Bitcoin SV protocol or by intentionally crafting blocks or 
transactions with unusual behavioural characteristics.

Bitcoin by design requires that miners competitively push the boundaries of resource limits to ensure ongoing growth in 
network capacity. As such default settings of various resource limiting features are intentionally defaulted to values 
which may not be considered safe under unusual situations. It is intended for operators of the Bitcoin SV software to 
choose and set these limits. Various other mechanisms, both technical and economic, are in place to discourage such 
attacks either by making them expensive to execute, by minimising their impact on the majority of network operations or 
by limiting resource usage with configurable consensus or policy limits.

Resource exhaustion attacks, as defined, are out of scope for the bug bounty program.

However, we acknowledge that there is value in documenting all possible attack vectors and will consider disclosures of 
such attacks for rewards in the "low" category and in exceptional cases in the "medium" category.  Awarding of bounties 
in this category are subject to the following conditions:

* The award is completely discretionary
* The attack must not be previously known to us
* The attack must be demonstrably executable on a version of the software that would otherwise be deemed in scope if not 
  for the resource exhaustion attack exclusion

For obvious security reasons it would not be responsible for the Bitcoin SV team to publicly document known attack vectors. 
This necessarily requires a degree of good faith however it is strongly in the interest of the Bitcoin SV team to 
encourage such disclosures and build trust with the security research community through building a track record of 
making such bounty awards.

Sensitive data
--------------

Please note, we do not want to receive any sensitive data during any disclosure, such as personally identifiable 
information (PII) or any data associated with private/public keys.

If in any doubt, send an email to `security@bitcoinsv.io`.

Bitcoin SV Security Team PGP Key
--------------------------------
```
-----BEGIN PGP PUBLIC KEY BLOCK-----

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
v7ed0V4eU8p5fzla7auhB+wyIkulnJbt
=1H3B
-----END PGP PUBLIC KEY BLOCK-----
```

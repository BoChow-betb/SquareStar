# Security

If you find a security issue, please avoid posting exploit details, API keys, or private user data in a public issue.

Use GitHub's private vulnerability-reporting/security-advisory flow when it is available for this repository. Include the affected version, a short reproduction, and the expected impact.

SquareStar stores a user-supplied Finnhub key and privacy-sensitive local state using Windows DPAPI. The project does not claim that DPAPI protects data from malware already running with the same Windows user's privileges.

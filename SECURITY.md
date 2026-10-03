# Security policy

## Supported versions

Security fixes go into the latest release only. That's firmware 0.11.x and
Android app 1.0.x at the moment.

## Reporting a vulnerability

Please **don't open a public issue** for a security problem.

Instead, report it privately: open the repository's **Security** tab and
choose **Report a vulnerability**. Only the maintainer can see these reports.
If that button isn't there, open an issue titled "Security contact" **without
any details**, and the maintainer will get in touch privately.

It helps to include:

- the firmware version (and app version, if the app is involved)
- what an attacker could do, and from where: the same Wi-Fi, the internet, or
  Bluetooth range
- the steps to reproduce it, or a proof of concept
- a suggested fix, if you have one

The maintainer will look at reports as soon as they can. Please allow a
reasonable amount of time for a fix before sharing details publicly.

## Known limits

netmon is built for a trusted home network. The behaviours below are
documented design choices (see the README's Security notes) rather than
vulnerabilities:

- The pages and the read-only API have no login, so anyone on the LAN can view
  them.
- Settings, restart and the Nearby controls refuse requests from pages on
  other sites but take no password. Only firmware updates need the update
  password.
- The `netmon-setup` network is open while the board is in setup mode.
- The ISP lookup uses plain HTTP.

Reports that go beyond these are very welcome, and so are ways around the
protections that do exist: the cross-site check and the update password.

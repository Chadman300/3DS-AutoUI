# Finding the OBD Adapter's IP Address on a Phone Hotspot

Use this when the V-Link OBD adapter has been configured to join a Wi-Fi
network (e.g., a phone hotspot) as a client instead of hosting its own
network. Its IP address will no longer be the default `192.168.0.10` once
it joins a different network, so it needs to be discovered.

## Requirements

- A laptop that can connect to the **same hotspot** the adapter joined.
- The adapter powered on (plugged into the car's OBD port, ignition on).

## Steps

1. Connect your laptop's Wi-Fi to the same phone hotspot the adapter is
   using.

2. Confirm you're on the hotspot and check your subnet:

   ```powershell
   ipconfig | Select-String -Pattern "Wireless LAN adapter Wi-Fi","IPv4 Address","Default Gateway" -Context 0,1
   ```

   Look for an IP like `172.20.10.x` (iPhone hotspot) or
   `192.168.43.x` / `192.168.49.x` (Android hotspot). Note the exact subnet.

3. Ping-sweep the subnet to populate the ARP table. This example assumes
   an iPhone hotspot's range (`172.20.10.1`-`172.20.10.14`, a `/28`
   subnet):

   ```powershell
   1..14 | ForEach-Object { Test-Connection -ComputerName "172.20.10.$_" -Count 1 -Quiet -ErrorAction SilentlyContinue | Out-Null }
   ```

   For a full `/24` Android hotspot subnet, sweep the whole range instead:

   ```powershell
   1..254 | ForEach-Object { Test-Connection -ComputerName "192.168.43.$_" -Count 1 -Quiet -ErrorAction SilentlyContinue | Out-Null }
   ```

4. List everything discovered:

   ```powershell
   arp -a
   ```

5. Look through the output for any entry besides your own laptop and the
   phone (the gateway, usually `.1`) — that remaining IP is almost
   certainly the OBD adapter. Cross-check the MAC address prefix for
   extra confidence if multiple unknown entries show up.

## Testing real TCP connectivity (not just ARP visibility)

ARP resolution succeeding does **not** guarantee actual TCP/IP traffic is
allowed between two hotspot clients — some phones enforce "client
isolation" that blocks device-to-device data while still allowing ARP.
To test the real thing, bind explicitly to the hotspot Wi-Fi interface
(don't rely on `Test-NetConnection` alone if the laptop also has an
Ethernet connection active — it may silently route through the wrong
interface):

```powershell
$client = New-Object System.Net.Sockets.TcpClient
$local = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse("172.20.10.3"), 0)
$client.Client.Bind($local)
try {
    $client.Connect("172.20.10.2", 35000)
    "TCP connect SUCCEEDED"
} catch {
    "TCP connect FAILED: $($_.Exception.Message)"
} finally {
    $client.Close()
}
```

Replace `172.20.10.3` with your laptop's own hotspot IP (check with
`ipconfig`) and `172.20.10.2` with the OBD adapter's discovered IP. If
this fails while ARP can see the adapter, it strongly suggests hotspot
client isolation is blocking the connection — a travel router (which
does not isolate its own clients) is the recommended workaround.

## After finding the IP

Update `host` in `ObdConnectionConfig` (`3ds_app/source/obd_client.hpp`)
to the discovered address, then rebuild with
`scripts/build_3ds.ps1`.

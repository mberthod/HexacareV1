<!-- Source: _sources/api-guides/ble/ble-qualification.rst.txt (ESP-IDF v6.0 documentation) -->

# Bluetooth<sup>®</sup> SIG Qualification

## Controller

The table below shows the latest qualification for Espressif Bluetooth LE Controller on each chip. For the qualification of Espressif modules, please check the [SIG Qualification Workspace](https://qualification.bluetooth.com/MyProjects/ListingsSearch).

<table>
<colgroup>
<col style="width: 101%" />
<col style="width: 28%" />
<col style="width: 14%" />
</colgroup>
<thead>
<tr class="header">
<th><p>Chip Name</p>
</th>
<th><p>Design Number /</p>
<p>Qualified Design ID<a href="#fn1" class="footnote-ref" id="fnref1" role="doc-noteref"><sup>1</sup></a></p>
</th>
<th><p>Specification</p>
<p>Version<a href="#fn2" class="footnote-ref" id="fnref2" role="doc-noteref"><sup>2</sup></a></p>
</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><p>ESP32</p>
<p>(Bluetooth LE Mode)</p></td>
<td>.. centered:: <a href="https://qualification.bluetooth.com/ListingDetails/98048">141661</a></td>
<td>.. centered:: 5.0</td>
</tr>
<tr class="even">
<td><p>ESP32</p>
<p>(Dual Mode: Bluetooth Classic &amp; Bluetooth LE)</p></td>
<td>.. centered:: <a href="https://qualification.bluetooth.com/ListingDetails/105426">147845</a></td>
<td>.. centered:: 4.2</td>
</tr>
<tr class="odd">
<td>ESP32-C2 (ESP8684)</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/160725">194009</a></p>
</td>
<td><p>5.3</p>
</td>
</tr>
<tr class="even">
<td>ESP32-C3</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/212759">239440</a></p>
</td>
<td><p>5.4</p>
</td>
</tr>
<tr class="odd">
<td>ESP32-C5</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/257081">Q331318</a></p>
</td>
<td><p>6.0</p>
</td>
</tr>
<tr class="even">
<td>ESP32-C6</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/262779">Q335877</a></p>
</td>
<td><p>6.0</p>
</td>
</tr>
<tr class="odd">
<td>ESP32-C61</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/257081">Q331318</a></p>
</td>
<td><p>6.0</p>
</td>
</tr>
<tr class="even">
<td>ESP32-S3</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/212759">239440</a></p>
</td>
<td><p>5.4</p>
</td>
</tr>
<tr class="odd">
<td>ESP32-H2</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/257081">Q331318</a></p>
</td>
<td><p>6.0</p>
</td>
</tr>
</tbody>
</table>
<aside id="footnotes" class="footnotes footnotes-end-of-document" role="doc-endnotes">
<hr />
<ol>
<li id="fn1"><p>Since 1 July 2024, the identifying number for a new qualified design has changed from Qualified Design ID (QDID) to <a href="https://qualification.support.bluetooth.com/hc/en-us/articles/26704417298573-What-do-I-need-to-know-about-the-new-Qualification-Program-Reference-Document-QPRD-v3#:~:text=The%20identifying%20number%20for%20a%20Design%20has%20changed%20from%20Qualified%20Design%20ID%20(QDID)%20to%20Design%20Number%20(DN)">Design Number (DN)</a>. Please log in to the <a href="https://www.bluetooth.com/">Bluetooth SIG website</a> to view Qualified Product Details, such as Design Details, TCRL Version, and ICS Details (passed cases) and etc.<a href="#fnref1" class="footnote-back" role="doc-backlink">↩︎</a></p></li>
<li id="fn2"><p>Some features of the Bluetooth Core Specification are optional. Therefore, passing the certification for a specific specification version does not necessarily mean supporting all the features specified in that version. Please refer to <code class="interpreted-text" role="doc">Major Feature Support Status &lt;ble-feature-support-status&gt;</code> for the supported Bluetooth LE features on each chip.<a href="#fnref2" class="footnote-back" role="doc-backlink">↩︎</a></p></li>
</ol>
</aside>

## Host

The table below shows the latest qualification for Espressif Bluetooth LE Host.

<table>
<thead>
<tr class="header">
<th><p>Host</p>
</th>
<th><p>Design Number / Qualified Design ID<a href="#fn1" class="footnote-ref" id="fnref1" role="doc-noteref"><sup>1</sup></a></p>
</th>
<th><p>Specification Version<a href="#fn2" class="footnote-ref" id="fnref2" role="doc-noteref"><sup>2</sup></a></p>
</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>ESP-Bluedroid</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/165785">198312</a></p>
</td>
<td><p>5.3</p>
</td>
</tr>
<tr class="even">
<td>ESP-NimBLE</td>
<td><p><a href="https://qualification.bluetooth.com/ListingDetails/310315">Q371597</a></p>
</td>
<td><p>6.1</p>
</td>
</tr>
</tbody>
</table>
<aside id="footnotes" class="footnotes footnotes-end-of-document" role="doc-endnotes">
<hr />
<ol>
<li id="fn1"><p>Since 1 July 2024, the identifying number for a new qualified design has changed from Qualified Design ID (QDID) to <a href="https://qualification.support.bluetooth.com/hc/en-us/articles/26704417298573-What-do-I-need-to-know-about-the-new-Qualification-Program-Reference-Document-QPRD-v3#:~:text=The%20identifying%20number%20for%20a%20Design%20has%20changed%20from%20Qualified%20Design%20ID%20(QDID)%20to%20Design%20Number%20(DN)">Design Number (DN)</a>. Please log in to the <a href="https://www.bluetooth.com/">Bluetooth SIG website</a> to view Qualified Product Details, such as Design Details, TCRL Version, and ICS Details (passed cases) and etc.<a href="#fnref1" class="footnote-back" role="doc-backlink">↩︎</a></p></li>
<li id="fn2"><p>Some features of the Bluetooth Core Specification are optional. Therefore, passing the certification for a specific specification version does not necessarily mean supporting all the features specified in that version. Please refer to <code class="interpreted-text" role="doc">Major Feature Support Status &lt;ble-feature-support-status&gt;</code> for the supported Bluetooth LE features on each chip.<a href="#fnref2" class="footnote-back" role="doc-backlink">↩︎</a></p></li>
</ol>
</aside>

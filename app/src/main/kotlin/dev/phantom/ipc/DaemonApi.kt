package dev.phantom.ipc

import com.google.gson.Gson
import com.google.gson.JsonObject
import com.google.gson.reflect.TypeToken
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.sse.EventSource
import okhttp3.sse.EventSourceListener
import okhttp3.sse.EventSources
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * DaemonApi — talks to phantom-daemon at localhost:7777.
 *
 * On-device: direct loopback (daemon runs on same phone).
 * Desktop:   run `adb forward tcp:7777 tcp:7777` first.
 */
class DaemonApi(private val baseUrl: String = "http://localhost:7777") {

    private val gson = Gson()
    private val json = "application/json".toMediaType()

    private val client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS)   // SSE: no read timeout
        .build()

    // ── Models ────────────────────────────────────────────────────────────────

    data class TrafficItem(
        val id:      Long    = 0,
        val host:    String  = "",
        val method:  String  = "",
        val path:    String  = "",
        val status:  Int     = 0,
        val proto:   String  = "",
        val bodyLen: Int     = 0,
        val body:    String  = "",
        val ts:      Long    = 0,
        val patched: Boolean = false,
    )

    data class PatchRule(
        val id:     Int     = 0,
        val path:   String  = "",
        val value:  String  = "",
        val url:    String  = "*",
        val active: Boolean = true,
    )

    data class ScanHit(
        val path:     String = "",
        val url:      String = "",
        val value:    String = "",
        val category: String = "",
        val label:    String = "",
    )

    data class Status(
        val alive: Boolean = false,
        val items: Int     = 0,
        val rules: Int     = 0,
    )

    // ── SSE ───────────────────────────────────────────────────────────────────

    fun streamTraffic(onItem: (TrafficItem) -> Unit,
                      onError: (Throwable) -> Unit): EventSource {
        val req = Request.Builder().url("$baseUrl/events").build()
        return EventSources.createFactory(client).newEventSource(req,
            object : EventSourceListener() {
                override fun onEvent(es: EventSource, id: String?,
                                     type: String?, data: String) {
                    try { onItem(gson.fromJson(data, TrafficItem::class.java)) }
                    catch (_: Exception) {}
                }
                override fun onFailure(es: EventSource, t: Throwable?, r: Response?) {
                    t?.let { onError(it) }
                }
            })
    }

    // ── REST ──────────────────────────────────────────────────────────────────

    fun getStatus(cb: (Status?, Throwable?) -> Unit) =
        get("$baseUrl/status") { body, err ->
            cb(body?.let { gson.fromJson(it, Status::class.java) }, err)
        }

    fun getTraffic(cb: (List<TrafficItem>?, Throwable?) -> Unit) =
        get("$baseUrl/traffic") { body, err ->
            val type = object : TypeToken<List<TrafficItem>>() {}.type
            cb(body?.let { gson.fromJson(it, type) }, err)
        }

    fun addPatch(path: String, value: String, url: String = "*",
                 cb: (PatchRule?, Throwable?) -> Unit) {
        val body = JsonObject().apply {
            addProperty("path",  path)
            addProperty("value", value)
            addProperty("url",   url)
        }.toString().toRequestBody(json)
        post("$baseUrl/patch", body) { resp, err ->
            cb(resp?.let { gson.fromJson(it, PatchRule::class.java) }, err)
        }
    }

    fun removePatch(id: Int, cb: (Boolean) -> Unit) {
        client.newCall(Request.Builder().url("$baseUrl/patch/$id").delete().build())
            .enqueue(object : Callback {
                override fun onFailure(c: Call, e: IOException) = cb(false)
                override fun onResponse(c: Call, r: Response) = cb(r.isSuccessful)
            })
    }

    fun scan(cb: (List<ScanHit>?, Throwable?) -> Unit) =
        post("$baseUrl/scan", "".toRequestBody()) { body, err ->
            val type = object : TypeToken<List<ScanHit>>() {}.type
            cb(body?.let { gson.fromJson(it, type) }, err)
        }

    // ── helpers ───────────────────────────────────────────────────────────────

    private fun get(url: String, cb: (String?, Throwable?) -> Unit) {
        client.newCall(Request.Builder().url(url).build())
            .enqueue(object : Callback {
                override fun onFailure(c: Call, e: IOException) = cb(null, e)
                override fun onResponse(c: Call, r: Response) =
                    cb(r.body?.string(), null)
            })
    }

    private fun post(url: String, body: RequestBody,
                     cb: (String?, Throwable?) -> Unit) {
        client.newCall(Request.Builder().url(url).post(body).build())
            .enqueue(object : Callback {
                override fun onFailure(c: Call, e: IOException) = cb(null, e)
                override fun onResponse(c: Call, r: Response) =
                    cb(r.body?.string(), null)
            })
    }
}

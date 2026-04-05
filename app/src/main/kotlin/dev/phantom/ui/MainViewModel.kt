package dev.phantom.ui

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dev.phantom.ipc.DaemonApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import okhttp3.sse.EventSource

class MainViewModel : ViewModel() {

    val api = DaemonApi()

    private val _traffic   = MutableLiveData<List<DaemonApi.TrafficItem>>(emptyList())
    private val _connected = MutableLiveData(false)
    private val _status    = MutableLiveData<DaemonApi.Status>()
    private val _scanHits  = MutableLiveData<List<DaemonApi.ScanHit>>()
    private val _filter    = MutableLiveData("")

    val traffic:   LiveData<List<DaemonApi.TrafficItem>> = _traffic
    val connected: LiveData<Boolean>                     = _connected
    val status:    LiveData<DaemonApi.Status>            = _status
    val scanHits:  LiveData<List<DaemonApi.ScanHit>>     = _scanHits

    private var sseSource: EventSource? = null
    private val items = ArrayDeque<DaemonApi.TrafficItem>()
    private val MAX   = 500

    fun connect() {
        sseSource?.cancel()
        sseSource = api.streamTraffic(
            onItem  = { item ->
                synchronized(items) {
                    items.addFirst(item)
                    if (items.size > MAX) items.removeLast()
                }
                _connected.postValue(true)
                publish()
            },
            onError = {
                _connected.postValue(false)
                viewModelScope.launch { delay(3_000); connect() }
            }
        )
    }

    fun disconnect() { sseSource?.cancel(); _connected.postValue(false) }

    fun loadHistory() {
        api.getTraffic { list, _ ->
            list?.let {
                synchronized(items) { items.clear(); items.addAll(it.reversed()) }
                publish()
            }
        }
    }

    fun refreshStatus() { api.getStatus { s, _ -> s?.let { _status.postValue(it) } } }

    fun setFilter(q: String) { _filter.value = q; publish() }

    fun scan() { api.scan { hits, _ -> _scanHits.postValue(hits ?: emptyList()) } }

    fun addPatch(path: String, value: String, url: String,
                 cb: (DaemonApi.PatchRule?) -> Unit) {
        api.addPatch(path, value, url) { r, _ -> cb(r) }
    }

    fun removePatch(id: Int) { api.removePatch(id) {} }

    private fun publish() {
        val q    = _filter.value?.lowercase() ?: ""
        val copy = synchronized(items) { items.toList() }
        _traffic.postValue(
            if (q.isEmpty()) copy
            else copy.filter {
                it.host.lowercase().contains(q) ||
                it.path.lowercase().contains(q) ||
                it.body.lowercase().contains(q)
            }
        )
    }

    override fun onCleared() { super.onCleared(); disconnect() }
}

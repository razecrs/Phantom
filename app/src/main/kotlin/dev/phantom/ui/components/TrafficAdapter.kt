package dev.phantom.ui.components

import android.graphics.Color
import android.view.*
import android.widget.TextView
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import dev.phantom.R
import dev.phantom.ipc.DaemonApi

class TrafficAdapter(
    private val onClick: (DaemonApi.TrafficItem) -> Unit
) : ListAdapter<DaemonApi.TrafficItem, TrafficAdapter.VH>(DIFF) {

    class VH(v: View) : RecyclerView.ViewHolder(v) {
        val method:  TextView = v.findViewById(R.id.tvMethod)
        val host:    TextView = v.findViewById(R.id.tvHost)
        val path:    TextView = v.findViewById(R.id.tvPath)
        val status:  TextView = v.findViewById(R.id.tvStatus)
        val patched: TextView = v.findViewById(R.id.tvPatched)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(LayoutInflater.from(parent.context)
            .inflate(R.layout.item_traffic, parent, false))

    override fun onBindViewHolder(h: VH, pos: Int) {
        val item = getItem(pos)
        h.method.text = item.method.ifEmpty { "RX" }
        h.host.text   = item.host
        h.path.text   = item.path.ifEmpty { "(raw)" }

        if (item.status > 0) {
            h.status.text = item.status.toString()
            h.status.setTextColor(when {
                item.status in 200..299 -> Color.parseColor("#3FB950")
                item.status in 400..499 -> Color.parseColor("#FF9800")
                item.status >= 500      -> Color.parseColor("#F85149")
                else                    -> Color.GRAY
            })
        } else {
            h.status.text = ""
        }

        h.patched.visibility =
            if (item.patched) View.VISIBLE else View.GONE

        /* colour method badge */
        val badgeColor = when (item.method.uppercase()) {
            "GET"    -> "#3FB950"
            "POST"   -> "#58A6FF"
            "DELETE" -> "#F85149"
            "PUT", "PATCH" -> "#FF9800"
            else     -> "#8B949E"
        }
        h.method.setBackgroundColor(Color.parseColor(badgeColor))

        h.itemView.setOnClickListener { onClick(item) }
    }

    companion object {
        private val DIFF = object : DiffUtil.ItemCallback<DaemonApi.TrafficItem>() {
            override fun areItemsTheSame(a: DaemonApi.TrafficItem, b: DaemonApi.TrafficItem) =
                a.id == b.id
            override fun areContentsTheSame(a: DaemonApi.TrafficItem, b: DaemonApi.TrafficItem) =
                a == b
        }
    }
}

package dev.phantom.ui.screens

import android.content.Intent
import android.os.Bundle
import android.view.Menu
import android.view.MenuItem
import android.widget.EditText
import android.widget.Toast
import androidx.activity.viewModels
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SearchView
import androidx.core.view.isVisible
import androidx.recyclerview.widget.LinearLayoutManager
import dev.phantom.R
import dev.phantom.databinding.ActivityMainBinding
import dev.phantom.ui.MainViewModel
import dev.phantom.ui.components.TrafficAdapter

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val vm: MainViewModel by viewModels()
    private lateinit var adapter: TrafficAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        setSupportActionBar(binding.toolbar)

        adapter = TrafficAdapter { item ->
            startActivity(Intent(this, TrafficDetailActivity::class.java).apply {
                putExtra("host",    item.host)
                putExtra("method",  item.method)
                putExtra("path",    item.path)
                putExtra("status",  item.status)
                putExtra("body",    item.body)
                putExtra("patched", item.patched)
            })
        }
        binding.recycler.layoutManager = LinearLayoutManager(this)
        binding.recycler.adapter = adapter

        vm.traffic.observe(this) { list ->
            adapter.submitList(list)
            binding.tvEmpty.isVisible = list.isEmpty()
            binding.tvCount.text = "${list.size} items"
        }
        vm.connected.observe(this) { ok ->
            binding.statusDot.setBackgroundResource(
                if (ok) R.drawable.dot_green else R.drawable.dot_red)
            binding.fabConnect.setImageResource(
                if (ok) R.drawable.ic_connected else R.drawable.ic_disconnected)
        }
        vm.status.observe(this) { s ->
            supportActionBar?.subtitle = "items: ${s.items}  rules: ${s.rules}"
        }
        vm.scanHits.observe(this) { hits ->
            if (hits.isEmpty()) {
                Toast.makeText(this, "No hits", Toast.LENGTH_SHORT).show()
                return@observe
            }
            AlertDialog.Builder(this)
                .setTitle("Scan hits (${hits.size})")
                .setMessage(hits.take(20).joinToString("\n") {
                    "• ${it.label}: ${it.path} = ${it.value}"
                })
                .setPositiveButton("Patch all") { _, _ ->
                    hits.forEach { h -> vm.addPatch(h.path, h.value, "*") {} }
                    Toast.makeText(this, "${hits.size} rules added", Toast.LENGTH_SHORT).show()
                }
                .setNegativeButton("Close", null)
                .show()
        }

        binding.fabConnect.setOnClickListener {
            if (vm.connected.value == true) vm.disconnect()
            else { vm.connect(); vm.loadHistory() }
        }
        binding.fabPatch.setOnClickListener { showPatchDialog() }

        vm.connect()
        vm.loadHistory()
        vm.refreshStatus()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_menu, menu)
        (menu.findItem(R.id.action_search).actionView as SearchView)
            .setOnQueryTextListener(object : SearchView.OnQueryTextListener {
                override fun onQueryTextSubmit(q: String) = true.also { vm.setFilter(q) }
                override fun onQueryTextChange(q: String) = true.also { vm.setFilter(q) }
            })
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem) = when (item.itemId) {
        R.id.action_scan  -> true.also { vm.scan() }
        R.id.action_clear -> true.also { vm.setFilter("") }
        else              -> super.onOptionsItemSelected(item)
    }

    private fun showPatchDialog() {
        val v = layoutInflater.inflate(R.layout.dialog_patch, null)
        AlertDialog.Builder(this)
            .setTitle("Add patch rule")
            .setView(v)
            .setPositiveButton("Add") { _, _ ->
                val path  = v.findViewById<EditText>(R.id.etPath).text.toString().trim()
                val value = v.findViewById<EditText>(R.id.etValue).text.toString().trim()
                val url   = v.findViewById<EditText>(R.id.etUrl).text.toString()
                    .trim().ifEmpty { "*" }
                if (path.isEmpty() || value.isEmpty()) {
                    Toast.makeText(this, "Path and value required", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                vm.addPatch(path, value, url) { rule ->
                    runOnUiThread {
                        Toast.makeText(this,
                            if (rule != null) "Rule added (id=${rule.id})" else "Failed",
                            Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
}

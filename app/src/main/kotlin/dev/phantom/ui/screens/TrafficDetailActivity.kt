package dev.phantom.ui.screens

import android.os.Bundle
import android.view.MenuItem
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import com.google.gson.GsonBuilder
import com.google.gson.JsonParser
import dev.phantom.databinding.ActivityDetailBinding

class TrafficDetailActivity : AppCompatActivity() {

    private lateinit var binding: ActivityDetailBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityDetailBinding.inflate(layoutInflater)
        setContentView(binding.root)
        setSupportActionBar(binding.toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        val host    = intent.getStringExtra("host")    ?: ""
        val method  = intent.getStringExtra("method")  ?: ""
        val path    = intent.getStringExtra("path")    ?: ""
        val status  = intent.getIntExtra("status", 0)
        val body    = intent.getStringExtra("body")    ?: ""
        val patched = intent.getBooleanExtra("patched", false)

        supportActionBar?.title    = "$method $path".trim()
        supportActionBar?.subtitle = host

        binding.tvPatched.visibility = if (patched) View.VISIBLE else View.GONE

        binding.tvMeta.text = buildString {
            if (host.isNotEmpty())   appendLine("Host:    $host")
            if (method.isNotEmpty()) appendLine("Method:  $method")
            if (path.isNotEmpty())   appendLine("Path:    $path")
            if (status > 0)          appendLine("Status:  $status")
            if (patched)             appendLine("Patched: YES — phantom modified this response")
        }

        binding.tvBody.text = prettyJson(body)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) { finish(); return true }
        return super.onOptionsItemSelected(item)
    }

    private fun prettyJson(s: String) = try {
        GsonBuilder().setPrettyPrinting().create()
            .toJson(JsonParser.parseString(s.trim()))
    } catch (_: Exception) { s }
}

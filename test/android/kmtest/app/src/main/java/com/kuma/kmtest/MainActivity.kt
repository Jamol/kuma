package com.kuma.kmtest

import android.os.Bundle
import com.google.android.material.snackbar.Snackbar
import androidx.appcompat.app.AppCompatActivity
import androidx.navigation.findNavController
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.navigateUp
import androidx.navigation.ui.setupActionBarWithNavController
import android.view.Menu
import android.view.MenuItem
import com.kuma.kmapi.HttpRequest
import com.kuma.kmapi.TcpSocket
import com.kuma.kmapi.UdpSocket
import com.kuma.kmapi.WebSocket
import com.kuma.kmtest.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var appBarConfiguration: AppBarConfiguration
    private lateinit var binding: ActivityMainBinding
    private lateinit var ws: WebSocket
    private lateinit var http: HttpRequest
    private lateinit var tcp: TcpSocket
    private lateinit var udp: UdpSocket

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)

        val navController = findNavController(R.id.nav_host_fragment_content_main)
        appBarConfiguration = AppBarConfiguration(navController.graph)
        setupActionBarWithNavController(navController, appBarConfiguration)

        binding.fab.setOnClickListener { view ->
            Snackbar.make(view, "Replace with your own action", Snackbar.LENGTH_LONG)
                .setAction("Action", null).show()
            ws = WebSocket.create("HTTP/1.1")
            ws.setListener {
                onOpen { err ->
                    println("ws.onOpen, err=$err")
                }
                onError { err->
                    println("ws.onError, err=$err")
                }
            }
            val wsUrl = "wss://demo.piesocket.com/v3/channel_123?api_key=VCXCEuvhGcBDP7XhiJJUDvR1e1D3eiVjgZ9VRiaV&notify_self"
            ws.open(wsUrl)

            http = HttpRequest.create("HTTP/2.0")
            http.setListener {
                onHeaderComplete {
                    println("http.onHeaderComplete")
                }
                onResponseComplete {
                    println("http.onResponseComplete")
                }
                onError { err->
                    println("http.onError, err=$err")
                }
            }
            http.sendRequest("GET", "https://www.cloudflare.com")

            tcp = TcpSocket.create()
            tcp.setListener {
                onConnect { err ->
                    println("tcp.onConnect, err=$err")
                }
                onData { data ->
                    println("tcp.onData, len=" + data.size)
                }
                onError { err->
                    println("tcp.onError, err=$err")
                }
            }
            tcp.connect("www.baidu.com", 80)

            udp = UdpSocket.create()
            udp.setListener {
                onData { data, host, port ->
                    println("udp.onData, len=" + data.size + ", dest=" + host + ":" + port)
                }
                onError { err->
                    println("udp.onError, err=$err")
                }
            }
            udp.bind("::1", 8080)
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        // Inflate the menu; this adds items to the action bar if it is present.
        menuInflater.inflate(R.menu.menu_main, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        return when (item.itemId) {
            R.id.action_settings -> true
            else -> super.onOptionsItemSelected(item)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        val navController = findNavController(R.id.nav_host_fragment_content_main)
        return navController.navigateUp(appBarConfiguration)
                || super.onSupportNavigateUp()
    }
}
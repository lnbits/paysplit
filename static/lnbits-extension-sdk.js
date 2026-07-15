;(function () {
  let bridgePortPromise = null
  const LOG_PREFIX = '[paysplit extension]'

  function createLNbitsExtensionClient({extensionId}) {
    const baseUrl = `/api/v1/ext/${extensionId}`

    return {
      context() {
        return bridgeRequest({action: 'context'})
      },

      notify(level, message) {
        return bridgeRequest({
          action: 'ui.notify',
          level,
          message: errorMessage(message)
        })
      },

      listWallets() {
        return request(`${baseUrl}/wallets`)
      },

      getSource(walletId) {
        return request(`${baseUrl}/sources/${encodeURIComponent(walletId)}`)
      },

      saveSource(payload) {
        return request(`${baseUrl}/sources`, {
          method: 'POST',
          body: payload
        })
      },

      deleteSource(walletId) {
        return request(`${baseUrl}/sources/${encodeURIComponent(walletId)}`, {
          method: 'DELETE'
        })
      },

      requestWalletPaymentWatchPermission(grant) {
        return bridgeRequest({
          action: 'permissions.request_wallet_payment_watch',
          grant
        })
      },

      requestBackgroundPaymentPermission(grant) {
        return bridgeRequest({
          action: 'permissions.request_background_payment',
          grant
        })
      }
    }
  }

  function request(path, {method = 'GET', body = null} = {}) {
    return bridgeRequest({
      action: 'api',
      method,
      path,
      body
    })
      .then(unwrapRuntimeResponse)
      .catch(error => {
        logFailure('API request failed.', {method, path, body, error})
        throw error
      })
  }

  function bridgeRequest(message) {
    if (window.parent === window) {
      const error = new Error('LNbits extension bridge is not available.')
      logFailure('Bridge unavailable.', {message, error})
      return Promise.reject(error)
    }

    return getBridgePort()
      .then(port => bridgePortRequest(port, message))
      .catch(error => {
        if (message.action !== 'api') {
          logFailure('Bridge request failed.', {message, error})
        }
        throw error
      })
  }

  function getBridgePort() {
    if (!bridgePortPromise) {
      bridgePortPromise = connectBridge()
    }
    return bridgePortPromise
  }

  function connectBridge() {
    const id = requestId()
    const channel = new MessageChannel()
    const parentOrigin = new URL(window.location.href).origin

    return new Promise((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        channel.port1.removeEventListener('message', onMessage)
        channel.port1.close()
        const error = new Error('LNbits extension bridge timed out.')
        logFailure('Bridge connection timed out.', {id, error})
        reject(error)
      }, 30000)

      function onMessage(event) {
        if (event.currentTarget !== channel.port1) return

        const response = event.data
        if (
          !response ||
          response.type !== 'lnbits-extension:connected' ||
          response.id !== id
        ) {
          return
        }

        window.clearTimeout(timeout)
        channel.port1.removeEventListener('message', onMessage)
        resolve(channel.port1)
      }

      channel.port1.addEventListener('message', onMessage)
      channel.port1.start()
      window.parent.postMessage(
        {
          type: 'lnbits-extension:connect',
          id
        },
        parentOrigin,
        [channel.port2]
      )
    })
  }

  function bridgePortRequest(port, message) {
    const id = requestId()

    return new Promise((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        port.removeEventListener('message', onMessage)
        const error = new Error('LNbits extension bridge timed out.')
        logFailure('Bridge request timed out.', {id, message, error})
        reject(error)
      }, 30000)

      function onMessage(event) {
        if (event.currentTarget !== port) return

        const response = event.data
        if (
          !response ||
          response.type !== 'lnbits-extension:response' ||
          response.id !== id
        ) {
          return
        }

        window.clearTimeout(timeout)
        port.removeEventListener('message', onMessage)
        if (response.ok === false) {
          const error = new Error(response.error || 'Extension call failed.')
          logFailure('Bridge response failed.', {id, message, response, error})
          reject(error)
          return
        }
        resolve(response.data)
      }

      port.addEventListener('message', onMessage)
      port.postMessage({
        type: 'lnbits-extension:request',
        id,
        ...message
      })
    })
  }

  function unwrapRuntimeResponse(value) {
    if (typeof value === 'string') {
      value = JSON.parse(value)
    }

    if (value && value.ok === false) {
      throw new Error(value.error || 'Extension call failed.')
    }

    if (value && value.ok === true && 'data' in value) {
      return value.data
    }

    return value
  }

  function requestId() {
    return (
      window.crypto?.randomUUID?.() ||
      `request_${Date.now()}_${Math.random().toString(36).slice(2)}`
    )
  }

  function logFailure(message, details = {}) {
    if (!window.console || typeof window.console.error !== 'function') return
    window.console.error(LOG_PREFIX, message, details)
  }

  function errorMessage(value) {
    return value instanceof Error ? value.message : String(value)
  }

  window.createLNbitsExtensionClient = createLNbitsExtensionClient
})()
